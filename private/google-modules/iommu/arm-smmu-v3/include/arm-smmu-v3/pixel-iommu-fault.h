/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _PIXEL_IOMMU_FAULT_H
#define _PIXEL_IOMMU_FAULT_H

#include <linux/types.h>

struct device;
struct iommu_domain;

typedef int (*pixel_iommu_fault_handler_t)(struct device *dev, unsigned long iova,
					 u32 pasid, int flags, void *token);

/*
 * Unrecoverable faults only. Handlers may sleep, but must not register or
 * unregister handlers, or attach/detach IOMMU domains. The SMMU holds its
 * attachment locks while reporting a fault.
 *
 * Unregister before freeing the token or unloading the handler's module.
 * Unregister waits for active callbacks. All three operations may sleep.
 */
int pixel_iommu_register_fault_handler(struct device *dev,
				       pixel_iommu_fault_handler_t handler, void *token);
void pixel_iommu_unregister_fault_handler(struct device *dev);
/* A fault may outlive its attachment; domain may be NULL. */
int pixel_iommu_report_fault(struct iommu_domain *domain, struct device *dev,
			     unsigned long iova, u32 pasid, int flags);

#endif /* _PIXEL_IOMMU_FAULT_H */
