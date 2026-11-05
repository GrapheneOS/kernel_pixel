// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021 - Google LLC */

#include <linux/kvm_host.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <soc/google/pkvm-s2mpu.h>
#include "s2mpu-module.h"

extern struct s2mpu_config kvm_nvhe_sym(s2mpu_config);
extern struct kvm_iommu_ops kvm_nvhe_sym(s2mpu_ops);
int kvm_nvhe_sym(s2mpu_hyp_init)(const struct pkvm_module_ops *ops);
void kvm_nvhe_sym(s2mpu_hyp_power)(struct user_pt_regs *regs);

#define hyp_config kvm_nvhe_sym(s2mpu_config)

static struct device *s2mpu_devices[S2MPU_MAX_DEVS];
/* Off-at-boot MMIO must remain untouched until the first power transition. */
#define S2MPU_POWER_UNCHANGED 2
static u8 s2mpu_power_state[S2MPU_MAX_DEVS];
static DEFINE_SPINLOCK(s2mpu_power_lock);
static unsigned int nr_devices;
static int power_hcall;
static bool hyp_ready;
static bool config_locked;

bool s2mpu_el2_ready(void)
{
	return READ_ONCE(hyp_ready);
}

static int s2mpu_device_id(struct device *dev)
{
	unsigned int i;

	for (i = 0; i < nr_devices; i++)
		if (s2mpu_devices[i] == dev)
			return i;
	return -ENODEV;
}

int pkvm_iommu_s2mpu_register(struct device *dev, phys_addr_t pa, u8 flags)
{
	struct s2mpu_dev_config *config;
	unsigned long irqflags;
	bool off_at_boot = of_property_present(dev->of_node, "off-at-boot");
	int ret = 0;

	if (!is_protected_kvm_enabled())
		return -ENODEV;
	if (!IS_ALIGNED(pa, PAGE_SIZE))
		return -EINVAL;

	spin_lock_irqsave(&s2mpu_power_lock, irqflags);
	if (config_locked || nr_devices == S2MPU_MAX_DEVS) {
		ret = -ENOSPC;
		goto out;
	}
	config = &hyp_config.devs[nr_devices];
	config->pa = pa;
	config->flags = flags;
	s2mpu_power_state[nr_devices] = off_at_boot ? S2MPU_POWER_UNCHANGED : 0;
	s2mpu_devices[nr_devices++] = dev;
	hyp_config.nr_devs = nr_devices;
out:
	spin_unlock_irqrestore(&s2mpu_power_lock, irqflags);
	return ret;
}

int pkvm_iommu_sysmmu_sync_register(struct device *dev, phys_addr_t pa,
				  struct device *parent)
{
	struct s2mpu_dev_config *config;
	unsigned long flags;
	int id, ret = 0;

	pa += SYSMMU_SYNC_S2_OFFSET;
	if (!IS_ALIGNED(pa, PAGE_SIZE))
		return -EINVAL;
	spin_lock_irqsave(&s2mpu_power_lock, flags);
	if (config_locked) {
		ret = -EBUSY;
		goto out;
	}
	id = s2mpu_device_id(parent);
	if (id < 0) {
		ret = id;
		goto out;
	}
	config = &hyp_config.devs[id];
	if (config->nr_syncs == S2MPU_MAX_SYNCS) {
		ret = -ENOSPC;
		goto out;
	}
	config->sync_pa[config->nr_syncs++] = pa;
out:
	spin_unlock_irqrestore(&s2mpu_power_lock, flags);
	return ret;
}

int s2mpu_el2_power(struct device *dev, bool on)
{
	unsigned long flags;
	int id, ret = 0;

	spin_lock_irqsave(&s2mpu_power_lock, flags);
	id = s2mpu_device_id(dev);
	if (id < 0) {
		ret = id;
		goto out;
	}
	if (hyp_ready)
		ret = pkvm_el2_mod_call(power_hcall, id, on);
	if (!ret)
		s2mpu_power_state[id] = on;
out:
	spin_unlock_irqrestore(&s2mpu_power_lock, flags);
	return ret;
}

static int s2mpu_apply_power_states(void)
{
	unsigned long flags;
	unsigned int i;
	int ret = 0;

	/* Serialize the snapshot handoff with runtime and PHY power callbacks. */
	spin_lock_irqsave(&s2mpu_power_lock, flags);
	for (i = 0; i < nr_devices; i++) {
		if (s2mpu_power_state[i] == S2MPU_POWER_UNCHANGED)
			continue;
		ret = pkvm_el2_mod_call(power_hcall, i, s2mpu_power_state[i]);
		if (ret)
			goto out;
	}
	WRITE_ONCE(hyp_ready, true);
out:
	spin_unlock_irqrestore(&s2mpu_power_lock, flags);
	return ret;
}

int s2mpu_el2_init(void)
{
	unsigned long smpt[NR_GIGABYTES] = {};
	unsigned long flags;
	const int order = SMPT_ORDER(MPT_PROT_BITS);
	pkvm_handle_t drv_id;
	unsigned int gb;
	int ret;

	/* Freeze registration before donating module data to EL2. */
	spin_lock_irqsave(&s2mpu_power_lock, flags);
	if (config_locked) {
		spin_unlock_irqrestore(&s2mpu_power_lock, flags);
		return -EBUSY;
	}
	config_locked = true;
	spin_unlock_irqrestore(&s2mpu_power_lock, flags);

	for_each_gb(gb) {
		smpt[gb] = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
		if (!smpt[gb]) {
			ret = -ENOMEM;
			goto free_smpt;
		}
		hyp_config.smpt_pa[gb] = __pa(smpt[gb]);
	}

	ret = pkvm_load_el2_module(kvm_nvhe_sym(s2mpu_hyp_init));
	if (ret)
		goto free_smpt;
	power_hcall = pkvm_register_el2_mod_call(kvm_nvhe_sym(s2mpu_hyp_power));
	if (power_hcall < 0) {
		ret = power_hcall;
		goto free_smpt;
	}

	ret = kvm_iommu_register_hyp_ops((struct kvm_iommu_ops *)
		pkvm_el2_mod_va(&kvm_nvhe_sym(s2mpu_ops)), &drv_id);
	if (ret) {
		/* EL2 may own a subset of the resources. Never free donated pages. */
		pr_err("S2MPU EL2 initialization failed: %d\n", ret);
		return ret;
	}
	ret = pkvm_el2_mod_call(power_hcall, 0, 2);
	if (ret)
		return ret;
	return s2mpu_apply_power_states();

free_smpt:
	for_each_gb(gb)
		if (smpt[gb])
			free_pages(smpt[gb], order);
	return ret;
}
