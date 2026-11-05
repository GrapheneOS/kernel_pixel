/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __S2MPU_MODULE_H__
#define __S2MPU_MODULE_H__

#include "kvm_s2mpu.h"

/* Bounds checked while collecting the DT and again before EL2 consumes it. */
#define S2MPU_MAX_DEVS 128
#define S2MPU_MAX_SYNCS 8

struct s2mpu_dev_config {
	phys_addr_t pa;
	phys_addr_t sync_pa[S2MPU_MAX_SYNCS];
	u32 nr_syncs;
	u8 flags;
};

struct s2mpu_config {
	phys_addr_t smpt_pa[NR_GIGABYTES];
	u32 nr_devs;
	struct s2mpu_dev_config devs[S2MPU_MAX_DEVS];
};

int s2mpu_el2_init(void);
int s2mpu_el2_power(struct device *dev, bool on);
bool s2mpu_el2_ready(void);

#endif
