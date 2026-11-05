// SPDX-License-Identifier: GPL-2.0-only
/* Per-device unrecoverable fault callbacks, independent of the DMA domain cookie. */
#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/samsung-iommu-fault.h>

struct samsung_iommu_fault_handler {
	struct list_head node;
	struct device *dev;
	samsung_iommu_fault_handler_t handler;
	void *token;
};

static LIST_HEAD(fault_handlers);
static DECLARE_RWSEM(fault_handlers_lock);

int samsung_iommu_register_fault_handler(struct device *dev,
				       samsung_iommu_fault_handler_t handler, void *token)
{
	struct samsung_iommu_fault_handler *entry, *other;
	int ret = 0;

	if (!dev || !handler)
		return -EINVAL;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	down_write(&fault_handlers_lock);
	list_for_each_entry(other, &fault_handlers, node) {
		if (other->dev == dev) {
			ret = -EBUSY;
			goto out_unlock;
		}
	}
	entry->dev = get_device(dev);
	entry->handler = handler;
	entry->token = token;
	list_add_tail(&entry->node, &fault_handlers);
out_unlock:
	up_write(&fault_handlers_lock);
	if (ret)
		kfree(entry);
	return ret;
}
EXPORT_SYMBOL_GPL(samsung_iommu_register_fault_handler);

void samsung_iommu_unregister_fault_handler(struct device *dev)
{
	struct samsung_iommu_fault_handler *entry;

	down_write(&fault_handlers_lock);
	list_for_each_entry(entry, &fault_handlers, node) {
		if (entry->dev == dev) {
			list_del(&entry->node);
			up_write(&fault_handlers_lock);
			put_device(entry->dev);
			kfree(entry);
			return;
		}
	}
	up_write(&fault_handlers_lock);
}
EXPORT_SYMBOL_GPL(samsung_iommu_unregister_fault_handler);

int samsung_iommu_report_fault(struct device *dev,
			       struct samsung_iommu_fault *fault)
{
	struct samsung_iommu_fault_handler *entry;
	int ret = -ENOSYS;

	down_read(&fault_handlers_lock);
	list_for_each_entry(entry, &fault_handlers, node) {
		if (entry->dev == dev) {
			ret = entry->handler(fault, entry->token);
			break;
		}
	}
	up_read(&fault_handlers_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(samsung_iommu_report_fault);

MODULE_DESCRIPTION("Pixel IOMMU unrecoverable fault callbacks");
MODULE_LICENSE("GPL");
