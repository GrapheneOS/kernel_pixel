// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Google Corp.
 *
 * Author:
 *  Howard.Yen <howardyen@google.com>
 *  Puma.Hsu   <pumahsu@google.com>
 */

#include <core/hub.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_domain.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/usb/hcd.h>
#include <linux/usb/quirks.h>
#include <linux/usb/google-role-sw.h>
#include <linux/usb/storage.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/device/bus.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "aoc_usb.h"
#include "xhci-exynos.h"
#include "xhci-plat.h"
#include "usb_offload.h"
#include <linux/list.h>
#include <linux/usb/xhci-sideband.h>

#define AOC_CORE_POWER_CTRL_TIMEOUT 1000
#define USB_ENUMERATION_TIMEOUT 5000

/* The index 0 for SRAM and 1 for DRAM reflect the memory region order declared in the dts */
#define MEM_SRAM 0
#define MEM_DRAM 1

static struct usb_offload_data *offload_data;

static int match_xhci_device(struct device *dev, const void *data)
{
	return strstarts(dev_name(dev), "xhci-hcd");
}

static void aoc_usb_xhci_reset_work_handler(struct work_struct *work)
{
	struct usb_offload_data *offload_data = container_of(work, struct usb_offload_data,
							     xhci_reset_work);
	struct device *xhci_dev;
	struct device *dwc3_dev;
	int ret;

	xhci_dev = bus_find_device(&platform_bus_type, NULL, NULL, match_xhci_device);
	if (!xhci_dev) {
		pr_err("%s: Cannot find xHCI host device to reset\n", __func__);
		offload_data->mem_swap_stat = STATE_MAPPED_SRAM;
		return;
	}

	dwc3_dev = xhci_dev->parent;
	if (!dwc3_dev) {
		dev_err(xhci_dev, "%s: Cannot find DWC3 parent device\n", __func__);
		put_device(xhci_dev);
		offload_data->mem_swap_stat = STATE_MAPPED_SRAM;
		return;
	}

	/* Prevent DWC3 from suspending */
	ret = pm_runtime_get_sync(dwc3_dev);
	if (ret < 0) {
		dev_err(dwc3_dev, "%s: failed to hold pm for DWC3, ret = %d\n", __func__, ret);
		pm_runtime_put_noidle(dwc3_dev);
		put_device(xhci_dev);
		offload_data->mem_swap_stat = STATE_MAPPED_SRAM;
		return;
	}

	offload_data->is_resetting_xhci = true;

	dev_info(xhci_dev, "%s: Starting XHCI reset sequence for %s\n", __func__,
		 dev_name(xhci_dev));

	/* Unbind XHCI */
	device_release_driver(xhci_dev);
	msleep(50);

	/* Abort attach if DWC3 became inactive (e.g. cable unplugged) */
	if (!pm_runtime_active(dwc3_dev)) {
		dev_warn(dwc3_dev, "%s: DWC3 inactive during reset(cable unplugged?)\n", __func__);
		offload_data->mem_swap_stat = STATE_MAPPED_SRAM;
		offload_data->is_resetting_xhci = false;
		pm_runtime_put_sync(dwc3_dev);
		put_device(xhci_dev);
		return;
	}

	/* Bind XHCI */
	ret = device_attach(xhci_dev);
	if (ret <= 0)
		dev_err(dwc3_dev, "%s: Failed to re-bind XHCI, ret = %d\n", __func__, ret);

	offload_data->is_resetting_xhci = false;
	/* Allow DWC3 to auto suspend again */
	pm_runtime_put_sync(dwc3_dev);
	put_device(xhci_dev);
	dev_info(xhci_dev, "%s: Finished XHCI reset sequence\n", __func__);
}

static int aoc_usb_reset_xhci(void)
{
	if (!offload_data)
		return -ENODEV;

	if (schedule_work(&offload_data->xhci_reset_work)) {
		pr_info("%s: XHCI reset work scheduled successfully\n", __func__);
		return 0;
	}

	pr_warn("%s: XHCI reset work is already pending or running, skipped\n", __func__);
	return -EBUSY;

}

static const char *parsed_device_string(enum parsed_usb_device usb_device)
{
	if (usb_device < 0 || usb_device >= ARRAY_SIZE(parsed_usb_devices))
		return parsed_usb_devices[DEVICE_UNKNOWN];

	return parsed_usb_devices[usb_device];
}
struct usb_offload_device {
	struct list_head list;
	struct usb_device *udev;
};

static void xhci_set_early_stop(struct usb_device *hdev)
{
	struct usb_hub *hub;
	struct usb_port *port_dev;

	if (!hdev->actconfig || !hdev->maxchild) {
		dev_err(&hdev->dev, "hdev is not active\n");
		return;
	}

	hub = usb_get_intfdata(hdev->actconfig->interface[0]);

	if (!hub) {
		dev_err(&hdev->dev, "can't get usb_hub\n");
		return;
	}

	port_dev = hub->ports[0];
	port_dev->early_stop = true;

	return;
}

/* TODO(b/455993010): Improve the ability to return all usb classes of the input usb device. */
static enum parsed_usb_device parse_connected_device(struct usb_device *udev)
{
	struct usb_endpoint_descriptor *epd;
	struct usb_host_config *config;
	struct usb_host_interface *alt;
	struct usb_interface_cache *intfc;
	int i, j, k;
	enum parsed_usb_device parsed_udev = DEVICE_GENERAL;

	if (is_root_hub(udev))
		return DEVICE_ROOT_HUB;

	config = udev->config;
	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		intfc = config->intf_cache[i];
		for (j = 0; j < intfc->num_altsetting; j++) {
			alt = &intfc->altsetting[j];

			if (alt->desc.bInterfaceClass == USB_CLASS_HUB) {
				parsed_udev = DEVICE_HUB;
				goto parse_done;
			}

			if (alt->desc.bInterfaceClass == USB_CLASS_MASS_STORAGE) {
				parsed_udev = DEVICE_STORAGE;
				/* check whether it supports streaming */
				if (alt->desc.bInterfaceProtocol == USB_PR_UAS)
					parsed_udev = DEVICE_STORAGE_STREAMING;

				goto parse_done;
			}

			if (alt->desc.bInterfaceClass == USB_CLASS_AUDIO) {
				parsed_udev = DEVICE_AUDIO;
				for (k = 0; k < alt->desc.bNumEndpoints; k++) {
					epd = &alt->endpoint[k].desc;
					if (usb_endpoint_xfer_isoc(epd)) {
						/* device contains ISOC endpoint */
						parsed_udev = DEVICE_AUDIO_ISOC;
						goto parse_done;
					}
				}
			}
		}
	}

parse_done:
	dev_info(&udev->dev, "Parsed the connected device: %s\n",
		 parsed_device_string(parsed_udev));
	return parsed_udev;
}

/* This function is copied from drivers/usb/core/buffer.c */
static void hcd_buffer_destroy_cp(struct usb_hcd *hcd)
{
	int i;

	if (!IS_ENABLED(CONFIG_HAS_DMA))
		return;

	for (i = 0; i < HCD_BUFFER_POOLS; i++) {
		dma_pool_destroy(hcd->pool[i]);
		hcd->pool[i] = NULL;
	}
}

#if IS_ENABLED(CONFIG_AOC_LGA)
static int aoc_core_pd_notifier(struct notifier_block *nb,
	unsigned long action, void *data)
{
	switch (action) {
	case GENPD_NOTIFY_ON:
		complete(&offload_data->aoc_core_pd_power_on);
		break;
	case GENPD_NOTIFY_OFF:
		complete(&offload_data->aoc_core_pd_power_off);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}
#endif

#if IS_ENABLED(CONFIG_SOC_GS101) || IS_ENABLED(CONFIG_SOC_GS201)

static int usb_offload_rmem_init(struct device *sysdev)
{
	int ret;

	ret = of_reserved_mem_device_init(sysdev);
	if (ret)
		dev_err(sysdev, "Could not get reserved memory index\n");

	return ret;
}

static void usb_offload_rmem_release(struct device *sysdev)
{
	of_reserved_mem_device_release(sysdev);
}

#else

static int usb_offload_rmem_init(struct device *sysdev)
{
	struct iommu_domain	*domain;
	struct reserved_mem	*rmem;
	struct device_node	*np;
	int i, count, ret;

	domain = iommu_get_domain_for_dev(sysdev);
	if (!domain) {
		dev_err(sysdev, "iommu domain not found.\n");
		ret = -ENOMEM;
		goto release_rmem;
	}

	/* When memory swap is requested, we only init reserved memory on DRAM */
	ret = of_reserved_mem_device_init_by_idx(sysdev, sysdev->of_node,
			offload_data->mem_swap_stat == STATE_SWAP_REQUESTED ? MEM_DRAM : MEM_SRAM);
	if (ret) {
		dev_err(sysdev, "Could not get reserved memory index : %d\n",
			offload_data->mem_swap_stat == STATE_SWAP_REQUESTED ? MEM_DRAM : MEM_SRAM);
		goto release_rmem;
	}

	count = of_property_count_elems_of_size(sysdev->of_node, "memory-region",
						sizeof(u32));

	/* When memory swap is requested, we only init reserved memory on DRAM */
	for (i = (offload_data->mem_swap_stat == STATE_SWAP_REQUESTED) ? MEM_DRAM : MEM_SRAM;
	     i < count; i++) {
		np = of_parse_phandle(sysdev->of_node, "memory-region", i);
		if (!np) {
			dev_err(sysdev, "memory-region not found\n");
			ret = -ENOMEM;
			goto unmap_iommu;
		}

		rmem = of_reserved_mem_lookup(np);
		of_node_put(np);
		if (!rmem) {
			dev_err(sysdev, "rmem lookup failed.\n");
			ret = -ENOMEM;
			goto unmap_iommu;
		}

		ret = iommu_map(domain, rmem->base, rmem->base, rmem->size,
				IOMMU_READ | IOMMU_WRITE, GFP_KERNEL);
		if (ret < 0) {
			dev_err(sysdev, "iommu_map error: %d\n", ret);
			goto unmap_iommu;
		}
	}

	return 0;

unmap_iommu:
	if (i > 0) {
		for (i = 0; i < count - 1; i++) {
			np = of_parse_phandle(sysdev->of_node, "memory-region", i);
			if (!np)
				continue;

			rmem = of_reserved_mem_lookup(np);
			of_node_put(np);
			if (!rmem)
				continue;

			iommu_unmap(domain, rmem->base, rmem->size);
		}
	}

release_rmem:
	of_reserved_mem_device_release(sysdev);

	return ret;
}

static void usb_offload_rmem_release(struct device *sysdev)
{
	struct iommu_domain	*domain;
	struct reserved_mem	*rmem;
	struct device_node	*np;
	int i, count, ret;

	domain = iommu_get_domain_for_dev(sysdev);
	count = of_property_count_elems_of_size(sysdev->of_node, "memory-region",
						sizeof(u32));

	for (i = 0; i < count; i++) {
		if (!domain)
			break;

		np = of_parse_phandle(sysdev->of_node, "memory-region", i);
		if (!np) {
			dev_err(sysdev, "memory-region not found.\n");
			continue;
		}

		rmem = of_reserved_mem_lookup(np);
		of_node_put(np);
		if (!rmem) {
			dev_err(sysdev, "rmem lookup failed.\n");
			continue;
		}

		ret = iommu_unmap(domain, rmem->base, rmem->size);
		if (ret < 0)
			dev_err(sysdev, "iommu_numap error: %d\n", ret);
	}

	of_reserved_mem_device_release(sysdev);
}
#endif

static void usb_audio_offload_cleanup(void)
{
	struct usb_offload_device *offload_dev, *tmp;
	struct device		*sysdev;
	struct usb_hcd *hcd;

	mutex_lock(&offload_data->offload_dev_lock);

	if (offload_data->offload_status == DISABLED) {
		mutex_unlock(&offload_data->offload_dev_lock);
		return;
	}

	list_for_each_entry_safe(offload_dev, tmp, &offload_data->offload_dev_list, list) {
		list_del(&offload_dev->list);
		kfree(offload_dev);
	}

	sysdev = offload_data->ubus->sysdev;
	hcd = bus_to_hcd(offload_data->ubus);
	offload_data->setup_notified = false;
	/* Notification for xhci driver removing */
	if (offload_data->offload_status == OFFLOAD_CONFIGURED)
		usb_host_mode_state_notify(USB_DISCONNECTED);

	/* We need to clean the pools before releasing the reserved memory */
	hcd_buffer_destroy_cp(hcd);
	usb_offload_rmem_release(sysdev);

#if IS_ENABLED(CONFIG_AOC_LGA)
	int timeout;
	pm_runtime_put_sync(offload_data->aoc_core_pd);

	timeout = wait_for_completion_timeout(&offload_data->aoc_core_pd_power_off,
		msecs_to_jiffies(AOC_CORE_POWER_CTRL_TIMEOUT));
	if (timeout == 0)
		dev_err(sysdev->parent, "timed out waiting for aoc_core_pd to power off\n");

	dev_pm_genpd_remove_notifier(offload_data->aoc_core_pd);

	dev_pm_domain_detach(offload_data->aoc_core_pd, false);
#endif
	offload_data->offload_status = DISABLED;
	offload_data->ubus = NULL;
	mutex_unlock(&offload_data->offload_dev_lock);
}

static int usb_audio_offload_init(struct usb_bus *ubus)
{
	struct device *dev = ubus->sysdev;
	int ret = 0;

	mutex_lock(&offload_data->offload_dev_lock);

	offload_data->wakeup_dev_count = 0;
	offload_data->total_dev_count = 0;

#if IS_ENABLED(CONFIG_AOC_LGA)
	offload_data->aoc_core_pd = dev_pm_domain_attach_by_name(dev->parent, "aoc_core_pd");
	if (!offload_data->aoc_core_pd) {
		dev_err(dev->parent, "Couldn't attach power domain aoc_core_pd\n");
		ret = -EINVAL;
		goto unlock;
	}

	offload_data->aoc_core_nb.notifier_call = aoc_core_pd_notifier;

	init_completion(&offload_data->aoc_core_pd_power_on);
	init_completion(&offload_data->aoc_core_pd_power_off);

	ret = dev_pm_genpd_add_notifier(offload_data->aoc_core_pd, &offload_data->aoc_core_nb);
	if (ret) {
		dev_err(dev->parent, "failed to add genpd notifier on aoc_core_pd, ret = %d\n",
			ret);
		goto err_genpd_add_notifier;
	}

	ret = pm_runtime_get_sync(offload_data->aoc_core_pd);
	if (ret < 0) {
		dev_err(dev->parent, "failed to call pm_runtime_get_sync on %s, ret = %d\n",
			dev_name(offload_data->aoc_core_pd), ret);
		goto err_pm_get;
	}

	if (!pm_runtime_active(offload_data->aoc_core_pd)) {
		ret = wait_for_completion_timeout(&offload_data->aoc_core_pd_power_on,
						msecs_to_jiffies(AOC_CORE_POWER_CTRL_TIMEOUT));
		if (ret == 0) {
			dev_err(dev->parent, "timed out waiting for aoc_core_pd to power on\n");
			ret = -ETIMEDOUT;
			goto err_aoc_core_pd_power_on;
		}
	}
#endif

	offload_data->memory_swap_enabled = of_property_present(dev->of_node,
								"memory-swap-enabled");
	if (offload_data->memory_swap_enabled) {
		dev_info(dev->parent, "Conditional memory swap is enabled\n");
	}

	offload_data->ubus = ubus;
	ret = usb_offload_rmem_init(dev);
	if (ret) {
		dev_err(dev, "Could not get reserved memory\n");
		goto err_rmem_init;
	}
	offload_data->offload_status = RMEM_CONFIGURED;

	/* Notification for xhci driver probing */
	ret = usb_host_mode_state_notify(USB_CONNECTED);
	if (ret) {
		dev_err(dev, "%s: Could not notify host mode state (ret = %d)\n", __func__, ret);
		goto unlock;
	}
	offload_data->setup_notified = false;

#if !IS_ENABLED(CONFIG_SOC_GS201)
	ret = xhci_setup_done();
	if (ret) {
		dev_err(dev, "%s: Could not setup xhci (ret = %d)\n", __func__, ret);
		goto unlock;
	}
	offload_data->setup_notified = true;
#endif

	offload_data->offload_status = OFFLOAD_CONFIGURED;
	mutex_unlock(&offload_data->offload_dev_lock);
	return ret;

err_rmem_init:
#if IS_ENABLED(CONFIG_AOC_LGA)
err_aoc_core_pd_power_on:
	pm_runtime_put_sync(offload_data->aoc_core_pd);

err_pm_get:
	dev_pm_genpd_remove_notifier(offload_data->aoc_core_pd);
err_genpd_add_notifier:
	dev_pm_domain_detach(offload_data->aoc_core_pd, false);
#endif
unlock:
	mutex_unlock(&offload_data->offload_dev_lock);
	return ret;
}

int usb_audio_offload_pause(void)
{
	struct usb_hcd *hcd;
	struct usb_device *udev;
	struct usb_offload_device *offload_dev;
	struct xhci_sideband *sb;
	struct xhci_virt_device *vdev;
	struct xhci_virt_ep *ep;
	struct xhci_hcd *xhci;
	int ret = 0;
	int i;

	mutex_lock(&offload_data->offload_dev_lock);
	if (!offload_data->ubus) {
		ret = -ENODEV;
		goto unlock;
	}

	hcd = bus_to_hcd(offload_data->ubus);

	if (!hcd) {
		ret = -ENODEV;
		goto unlock;
	}

	xhci = hcd_to_xhci(hcd);

	if (!xhci) {
		ret = -ENODEV;
		goto unlock;
	}

	list_for_each_entry(offload_dev, &offload_data->offload_dev_list, list) {
		udev = offload_dev->udev;

		sb = NULL;
		spin_lock_irq(&xhci->lock);
		if (udev->slot_id && xhci->devs[udev->slot_id]) {
			vdev = xhci->devs[udev->slot_id];
			if (vdev->sideband)
				sb = vdev->sideband;
		}
		spin_unlock_irq(&xhci->lock);

		if (sb) {
			mutex_lock(&sb->mutex);
			for (i = 0; i < EP_CTX_PER_DEV; i++) {
				if (sb->eps[i]) {
					ep = sb->eps[i];
					xhci_stop_endpoint_sync(sb->xhci, ep, 0, GFP_KERNEL);
					ep->sideband = NULL;
					sb->eps[ep->ep_index] = NULL;
				}
			}
			mutex_unlock(&sb->mutex);
			xhci_sideband_remove_interrupter(sb);
		}
	}

	if (offload_data->offload_status == OFFLOAD_CONFIGURED)
		offload_data->offload_status = RMEM_CONFIGURED;

unlock:
	mutex_unlock(&offload_data->offload_dev_lock);

	return ret;
}

int usb_audio_offload_resume(void)
{
	struct usb_offload_device *iter_dev;
	int ret = 0;

	mutex_lock(&offload_data->offload_dev_lock);
	if (offload_data->offload_status != RMEM_CONFIGURED)
		goto unlock;
	if (!offload_data->aoc_ready) {
		ret = -EINVAL;
		goto unlock;
	}
	pm_runtime_get_sync(offload_data->ubus->controller);

	/* Notification for xhci driver probing */
	ret = usb_host_mode_state_notify(USB_CONNECTED);
	if (ret)
		dev_err(offload_data->ubus->sysdev,
			"%s: Could not notify host mode state (ret = %d)\n", __func__, ret);
	ret = xhci_setup_done();
	if (ret)
		dev_err(offload_data->ubus->sysdev,
			"%s: Could not setup xhci (ret = %d)\n", __func__, ret);

	offload_data->offload_status = OFFLOAD_CONFIGURED;

	dev_info(offload_data->ubus->sysdev, "Re-syncing all compatible audio devices\n");
	list_for_each_entry(iter_dev, &offload_data->offload_dev_list, list) {
		ret = xhci_sync_conn_stat(iter_dev->udev->bus->busnum,
					  iter_dev->udev->devnum,
					  iter_dev->udev->slot_id,
					  USB_CONNECTED);
		if (ret)
			dev_warn(offload_data->ubus->sysdev,
				 "xhci_sync_conn_stat failed, ret = %d\n", ret);
	}
	pm_runtime_put_sync(offload_data->ubus->controller);
unlock:
	mutex_unlock(&offload_data->offload_dev_lock);

	return ret;
}

static void usb_offload_update_pm_state(struct usb_device *udev,
					enum parsed_usb_device parsed_udev, bool is_add)
{
	struct device *xhci_dev = udev->bus->root_hub->dev.parent;
	bool allow_sleep = false;

	if (parsed_udev == DEVICE_ROOT_HUB)
		return;

	mutex_lock(&offload_data->offload_dev_lock);
	if (is_add) {
		if (parsed_udev == DEVICE_AUDIO_ISOC && device_can_wakeup(&udev->dev)) {
			device_set_wakeup_enable(&udev->dev, 1);
			usb_enable_autosuspend(udev);
			offload_data->wakeup_dev_count++;
		} else {
			usb_disable_autosuspend(udev);
		}
		offload_data->total_dev_count++;
		pm_runtime_allow(xhci_dev);
	} else { /* REMOVE */
		if (offload_data->total_dev_count > 0)
			offload_data->total_dev_count--;
		if (parsed_udev == DEVICE_AUDIO_ISOC && device_can_wakeup(&udev->dev)) {
			if (offload_data->wakeup_dev_count > 0)
				offload_data->wakeup_dev_count--;
		}

		if (offload_data->total_dev_count == 0)
			pm_runtime_forbid(xhci_dev);
	}

	allow_sleep = (offload_data->wakeup_dev_count == 1 && offload_data->total_dev_count == 1)
		      || offload_data->total_dev_count == 0;

	if (allow_sleep)
		__pm_relax(offload_data->wakelock);
	else
		__pm_stay_awake(offload_data->wakelock);
	mutex_unlock(&offload_data->offload_dev_lock);

	dev_info(&udev->dev, "device %s, %s wakelock\n",
		 is_add ? "added" : "removed",
		 allow_sleep ? "release" : "acquire");
}

static void aoc_usb_handle_memswap_add(struct usb_device *udev,
				       enum parsed_usb_device parsed_udev)
{
	if (!offload_data->memory_swap_enabled)
		return;

	/*
	 * Trigger memory region swapping only when the connected device
	 * is a hub or a storage that supports streaming.
	 */
	if (parsed_udev != DEVICE_HUB && parsed_udev != DEVICE_STORAGE_STREAMING)
		return;

	if (offload_data->mem_swap_stat == STATE_SWAP_REQUESTED) {
		dev_info(&udev->dev, "Succeeded memory swap for %s\n",
			 parsed_device_string(parsed_udev));
		offload_data->mem_swap_stat = STATE_MAPPED_DRAM;
	} else if (offload_data->mem_swap_stat == STATE_MAPPED_SRAM) {
		dev_info(&udev->dev, "Trigger memory swap for %s\n",
			 parsed_device_string(parsed_udev));
		offload_data->mem_swap_stat = STATE_SWAP_REQUESTING;
		aoc_usb_reset_xhci();
	}
}

static void aoc_usb_handle_memswap_remove(void)
{
	if (!offload_data->memory_swap_enabled)
		return;

	/*
	 * Only transition to REQUESTED if this bus remove was triggered
	 * by our active XHCI reset unbind (is_resetting_xhci == true)
	 * and currently REQUESTING.
	 * Otherwise (e.g. physical cable unplug), reset state back to SRAM.
	 */
	if (offload_data->mem_swap_stat == STATE_SWAP_REQUESTING &&
	    offload_data->is_resetting_xhci) {
		pr_info("%s: Memory swap in progress, switch state to REQUESTED\n", __func__);
		offload_data->mem_swap_stat = STATE_SWAP_REQUESTED;
	} else {
		pr_info("%s: USB bus remove detected, switch state to default\n", __func__);
		offload_data->mem_swap_stat = STATE_MAPPED_SRAM;
	}
}

static int xhci_udev_notify(struct notifier_block *self, unsigned long action,
			    void *data)
{
	struct usb_device	*udev;
	struct usb_bus		*ubus;
	int ret;
	enum parsed_usb_device parsed_udev;

	switch (action) {
	case USB_DEVICE_ADD:
		udev = data;
		parsed_udev = parse_connected_device(udev);
		if (parsed_udev == DEVICE_ROOT_HUB) {
			udev->quirks = udev->quirks | USB_QUIRK_SHORT_SET_ADDRESS_REQ_TIMEOUT;
			xhci_set_early_stop(udev);
		} else if (parsed_udev == DEVICE_AUDIO_ISOC) {
			struct usb_offload_device *offload_dev;

			dev_dbg(&udev->dev, "Compatible with usb audio offload\n");
#if IS_ENABLED(CONFIG_SOC_GS201)
			if (!offload_data->setup_notified) {
				ret = xhci_setup_done();
				if (ret)
					dev_err(&udev->dev,
						"Could not setup xhci (ret = %d)\n", ret);
				offload_data->setup_notified = true;
			}
#endif
			offload_dev = kmalloc(sizeof(*offload_dev), GFP_KERNEL);
			if (offload_dev) {
				offload_dev->udev = udev;
				mutex_lock(&offload_data->offload_dev_lock);
				list_add_tail(&offload_dev->list, &offload_data->offload_dev_list);
				mutex_unlock(&offload_data->offload_dev_lock);
				ret = xhci_sync_conn_stat(udev->bus->busnum, udev->devnum,
							  udev->slot_id, USB_CONNECTED);
				if (ret)
					dev_warn(&udev->dev,
						 "xhci_sync_conn_stat failed, ret = %d\n", ret);
			}
		}

		aoc_usb_handle_memswap_add(udev, parsed_udev);
		usb_offload_update_pm_state(udev, parsed_udev, true);
		break;
	case USB_DEVICE_REMOVE:
		udev = data;
		parsed_udev = parse_connected_device(udev);
		if (parsed_udev == DEVICE_AUDIO_ISOC) {
			struct usb_offload_device *offload_dev, *tmp;
			bool found = false;

			mutex_lock(&offload_data->offload_dev_lock);
			list_for_each_entry_safe(offload_dev, tmp,
						 &offload_data->offload_dev_list, list) {
				if (offload_dev->udev == udev) {
					list_del(&offload_dev->list);
					kfree(offload_dev);
					found = true;
					break;
				}
			}
			mutex_unlock(&offload_data->offload_dev_lock);

			if (found) {
				ret = xhci_sync_conn_stat(udev->bus->busnum, udev->devnum,
							  udev->slot_id, USB_DISCONNECTED);
				if (ret)
					dev_warn(&udev->dev,
						 "xhci_sync_conn_stat failed, ret = %d\n", ret);
			} else {
				dev_err(&udev->dev,
					"Device not found in offload list for removal\n");
			}
		}
		usb_offload_update_pm_state(udev, parsed_udev, false);
		break;
	case USB_BUS_ADD:
		ubus = data;
		pm_wakeup_event(ubus->sysdev, USB_ENUMERATION_TIMEOUT);
		if (ubus->busnum == 1) {
			ret = usb_audio_offload_init(ubus);
			if (ret) {
				dev_err(ubus->sysdev, "offload init failed, ret = %d\n", ret);
				return ret;
			}
		}
		break;
	case USB_BUS_REMOVE:
		ubus = data;
		if (ubus->busnum == 1) {
			usb_audio_offload_cleanup();
			aoc_usb_handle_memswap_remove();
		}
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block xhci_udev_nb = {
	.notifier_call = xhci_udev_notify,
};

int usb_offload_helper_init(void)
{
	offload_data = kzalloc(sizeof(struct usb_offload_data), GFP_KERNEL);
	if (!offload_data)
		return -ENOMEM;

	offload_data->aoc_ready = false;
	INIT_LIST_HEAD(&offload_data->offload_dev_list);
	mutex_init(&offload_data->offload_dev_lock);

	offload_data->wakelock = wakeup_source_register(NULL, "usb_offload");
	offload_data->mem_swap_stat = STATE_MAPPED_SRAM;

	INIT_WORK(&offload_data->xhci_reset_work, aoc_usb_xhci_reset_work_handler);

	usb_register_notify(&xhci_udev_nb);

	return 0;
}

void usb_offload_helper_exit(void)
{
	struct usb_offload_device *offload_dev, *tmp;

	usb_unregister_notify(&xhci_udev_nb);

	if (offload_data)
		cancel_work_sync(&offload_data->xhci_reset_work);

	wakeup_source_unregister(offload_data->wakelock);

	mutex_lock(&offload_data->offload_dev_lock);
	list_for_each_entry_safe(offload_dev, tmp, &offload_data->offload_dev_list, list) {
		list_del(&offload_dev->list);
		kfree(offload_dev);
	}
	mutex_unlock(&offload_data->offload_dev_lock);

	kfree(offload_data);
	offload_data = NULL;
}

bool usb_offload_get_aoc_ready(void)
{
	return offload_data ? offload_data->aoc_ready : false;
}

int usb_offload_set_aoc_ready(bool is_ready)
{
	if (!offload_data)
		return -ENODEV;

	offload_data->aoc_ready = is_ready;

	return 0;
}
