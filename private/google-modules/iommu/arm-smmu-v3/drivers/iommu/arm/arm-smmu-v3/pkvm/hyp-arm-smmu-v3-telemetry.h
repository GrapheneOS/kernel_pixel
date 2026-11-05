/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HYP_ARM_SMMU_V3_PKVM_TELEMETRY_H
#define _HYP_ARM_SMMU_V3_PKVM_TELEMETRY_H

#include "arm-smmu-v3/arm-smmu-v3-telemetry-callbacks.h"
#include "hyp-arm-smmu-v3-common-telemetry.h"

struct hyp_arm_smmu_v3_device;

extern const struct arm_smmu_v3_telemetry_cb smmu_telemetry_cb;

void smmu_init_telemetry(u64 arch_timer_rate);
void smmu_telemetry_inc_s2_tlb_invals(void);
void smmu_telemetry_cmdq_sync_latency(struct hyp_arm_smmu_v3_device *smmu, u64 timer_tick_diff);
struct hyp_arm_smmu_domain_telemetry *domain_id_to_hasdt(pkvm_handle_t domain_id);
void smmu_telemetry_init_domain_data(pkvm_handle_t domain_id);
void smmu_telemetry_rec_idmap_snapshot(void);
bool smmu_phys_is_dram(phys_addr_t phys);
void smmu_telemetry_req_idmap_map(size_t size, bool snapshot_done);
void smmu_telemetry_req_idmap_unmap(size_t size, bool snapshot_done);

#endif /* _HYP_ARM_SMMU_V3_PKVM_TELEMETRY_H */
