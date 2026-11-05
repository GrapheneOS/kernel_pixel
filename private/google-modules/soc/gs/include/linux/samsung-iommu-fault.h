/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_SAMSUNG_IOMMU_FAULT_H
#define _LINUX_SAMSUNG_IOMMU_FAULT_H

#include <linux/types.h>

struct device;

enum samsung_iommu_fault_reason {
	SAMSUNG_IOMMU_FAULT_WALK_EABT,
	SAMSUNG_IOMMU_FAULT_PTE_FETCH,
	SAMSUNG_IOMMU_FAULT_ACCESS,
	SAMSUNG_IOMMU_FAULT_PASID_FETCH,
	SAMSUNG_IOMMU_FAULT_UNKNOWN,
};

struct samsung_iommu_fault {
	enum samsung_iommu_fault_reason reason;
	u64 addr;
	u32 pasid;
	u32 flags;
	u32 perm;
};

typedef int (*samsung_iommu_fault_handler_t)(struct samsung_iommu_fault *fault,
					  void *token);

/*
 * Unrecoverable faults only. Callbacks may sleep, but must not register or
 * unregister callbacks or change IOMMU attachments. Unregister waits for
 * active callbacks and must precede freeing the token or unloading its owner.
 */
int samsung_iommu_register_fault_handler(struct device *dev,
		samsung_iommu_fault_handler_t handler, void *token);
void samsung_iommu_unregister_fault_handler(struct device *dev);
int samsung_iommu_report_fault(struct device *dev,
			       struct samsung_iommu_fault *fault);

#endif
