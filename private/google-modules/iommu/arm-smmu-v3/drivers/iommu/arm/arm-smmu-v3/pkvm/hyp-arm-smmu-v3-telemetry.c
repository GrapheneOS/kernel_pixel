// SPDX-License-Identifier: GPL-2.0-only

#include <linux/types.h>
#include <asm/kvm_pkvm_module.h>
#include <nvhe/spinlock.h>

#include "arm_smmu_v3.h"
#include "arm-smmu-v3-module.h"
#include "hyp-arm-smmu-v3-telemetry.h"

/* Telemetry lock to protect global telemetry data */
static hyp_spinlock_t telemetry_lock;

struct hyp_shared_arm_smmu_telemetry *kvm_hyp_shared_arm_smmu_telemetry;

static bool smmu_telemetry_is_enable(void)
{
	return !!(kvm_hyp_shared_arm_smmu_telemetry->enabled);
}

/* Callback APIs exported to arm-smmu-v3.c for io_pgtable*/
static void smmu_telemetry_s1_pgtable_in_use(int count)
{
	if (!smmu_telemetry_is_enable())
		return;

	hyp_spin_lock(&telemetry_lock);
	kvm_hyp_shared_arm_smmu_telemetry->cur_s1_pgtable_usage += count;
	kvm_hyp_shared_arm_smmu_telemetry->max_s1_pgtable_usage =
			max(kvm_hyp_shared_arm_smmu_telemetry->max_s1_pgtable_usage,
			    kvm_hyp_shared_arm_smmu_telemetry->cur_s1_pgtable_usage);
	hyp_spin_unlock(&telemetry_lock);
}

static void smmu_telemetry_atomic_pages(int count)
{
	if (!smmu_telemetry_is_enable())
		return;

	if (count > 0)
		kvm_hyp_shared_arm_smmu_telemetry->hs2t.s2_atomic_pages.alloc_reqs++;
	else if (count < 0)
		kvm_hyp_shared_arm_smmu_telemetry->hs2t.s2_atomic_pages.free_reqs++;

	kvm_hyp_shared_arm_smmu_telemetry->hs2t.s2_atomic_pages.pages_in_use += count;
	kvm_hyp_shared_arm_smmu_telemetry->hs2t.s2_atomic_pages.max_pages_used =
		max(kvm_hyp_shared_arm_smmu_telemetry->hs2t.s2_atomic_pages.max_pages_used,
		    kvm_hyp_shared_arm_smmu_telemetry->hs2t.s2_atomic_pages.pages_in_use);
}

const struct arm_smmu_v3_telemetry_cb smmu_telemetry_cb = {
	.is_enabled = smmu_telemetry_is_enable,
	.s1_pages_tel = smmu_telemetry_s1_pgtable_in_use,
	.atomic_pages_tel = smmu_telemetry_atomic_pages,
};

/* Telemetry APIs called from arm-smmu-v3.c */
void smmu_init_telemetry(u64 arch_timer_rate)
{
	for (int i = 0; i < MAX_SMMU_DOMAIN; i++) {
		struct hyp_arm_smmu_domain_telemetry *hasdt;

		hasdt = &kvm_hyp_shared_arm_smmu_telemetry->hyp_dom_tel_arr[i];
		hasdt->map_counters.pgt_version = U64_MAX;
	}

	kvm_hyp_shared_arm_smmu_telemetry->hs2t.s2_map_counters.pgt_version = U64_MAX;
	kvm_hyp_shared_arm_smmu_telemetry->arch_timer_rate = arch_timer_rate;
	hyp_spin_lock_init(&telemetry_lock);
}

void smmu_telemetry_inc_s2_tlb_invals(void)
{
	if (!smmu_telemetry_is_enable())
		return;

	kvm_hyp_shared_arm_smmu_telemetry->hs2t.num_s2_tlb_invalidates++;
}

static u32 smmu_dev_to_id(struct hyp_arm_smmu_v3_device *smmu)
{
	return (smmu - kvm_hyp_arm_smmu_v3_smmus);
}

void smmu_telemetry_cmdq_sync_latency(struct hyp_arm_smmu_v3_device *smmu, u64 timer_tick_diff)
{
	struct hyp_arm_smmu_device_telemetry *telemetry;
	u32 index = smmu_dev_to_id(smmu);

	if (index >= MAX_SMMU_DEVICE) {
		WARN_ON(1);
		return;
	}
	telemetry = &kvm_hyp_shared_arm_smmu_telemetry->hyp_dev_tel_arr[index];

	telemetry->cmdq_tel.sync_cmd_cnt++;
	telemetry->cmdq_tel.sync_cmd_total_timer_tick += timer_tick_diff;
	telemetry->cmdq_tel.sync_cmd_max_timer_tick =
		max(timer_tick_diff, telemetry->cmdq_tel.sync_cmd_max_timer_tick);
}

struct hyp_arm_smmu_domain_telemetry *domain_id_to_hasdt(pkvm_handle_t domain_id)
{
	if (domain_id >= MAX_SMMU_DOMAIN) {
		/*
		 * This is fatal. Number of domains are more than what's defined in MAX_SMMU_DOMAIN.
		 * Increase MAX_SMMU_DOMAIN sufficiently.
		 */
		WARN_ON(1);
		return NULL;
	}

	return &kvm_hyp_shared_arm_smmu_telemetry->hyp_dom_tel_arr[domain_id];
}

void smmu_telemetry_init_domain_data(pkvm_handle_t domain_id)
{
	struct hyp_arm_smmu_domain_telemetry *hasdt;

	if (!smmu_telemetry_is_enable())
		return;

	hasdt = domain_id_to_hasdt(domain_id);
	if (!hasdt)
		return;

	memset(hasdt, 0, sizeof(*hasdt));
	hasdt->map_counters.pgt_version = U64_MAX;
}

void smmu_telemetry_rec_idmap_snapshot(void)
{
	struct hyp_stage2_telemetry *hs2t;
	u64 prot_mem_usage = 0;

	if (!smmu_telemetry_is_enable())
		return;

	hs2t = &kvm_hyp_shared_arm_smmu_telemetry->hs2t;
	/*
	 * Right after snapshotting stage 2 and before any donations, we know exactly
	 * how much memory host is using. Any dram not mapped by host should belong to protected.
	 */
	prot_mem_usage = hs2t->total_dram - hs2t->host_mem_usage;
	hs2t->max_prot_mem_usage = max(hs2t->max_prot_mem_usage, prot_mem_usage);
}

bool smmu_phys_is_dram(phys_addr_t phys)
{
	u32 i;
	struct hyp_stage2_telemetry *hs2t;

	if (!kvm_hyp_shared_arm_smmu_telemetry)
		return false;

	hs2t = &kvm_hyp_shared_arm_smmu_telemetry->hs2t;
	/* num_dram_regions is expected to be much less than 128 */
	for (i = 0; i < hs2t->num_dram_regions; i++) {
		if (phys >= hs2t->dram_regions[i].start &&
		    phys <= hs2t->dram_regions[i].end)
			return true;
	}

	return false;
}

void smmu_telemetry_req_idmap_map(size_t size, bool snapshot_done)
{
	struct hyp_stage2_telemetry *hs2t;

	if (!smmu_telemetry_is_enable())
		return;

	hs2t = &kvm_hyp_shared_arm_smmu_telemetry->hs2t;
	hs2t->host_mem_usage += size;
}

void smmu_telemetry_req_idmap_unmap(size_t size, bool snapshot_done)
{
	struct hyp_stage2_telemetry *hs2t;
	u64 prot_mem_usage = 0;

	if (!smmu_telemetry_is_enable())
		return;

	hs2t = &kvm_hyp_shared_arm_smmu_telemetry->hs2t;
	hs2t->host_mem_usage -= size;
	if (snapshot_done) {
		prot_mem_usage = hs2t->total_dram - hs2t->host_mem_usage;
		hs2t->max_prot_mem_usage = max(prot_mem_usage, hs2t->max_prot_mem_usage);
	}
}
