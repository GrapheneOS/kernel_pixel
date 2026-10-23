// SPDX-License-Identifier: GPL-2.0-only
/*
 * SLC operations for Codec3P
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Jerry Huang <huangjerry@google.com>
 */


#include <linux/module.h>

#include "vpu_priv.h"
#include "vpu_coredump.h"


static void vpu_core_sscd_release(struct device *dev) {}

int vpu_sscd_dev_register(struct vpu_core *core)
{
	int res;

	struct platform_device *pdev = &core->sscd_pdev;

	pdev->name = VPU_CORE_NAME;
	pdev->id = PLATFORM_DEVID_NONE;
	pdev->dev.platform_data = &core->vpu_core_sscd_platdata;
	pdev->dev.release = vpu_core_sscd_release;

	device_initialize(&pdev->dev);
	res = device_set_driver_override(&pdev->dev, SSCD_NAME);
	if (!res)
		res = platform_device_add(pdev);
	if (res)
		platform_device_put(pdev);

	return res;
}

void vpu_sscd_dev_unregister(struct vpu_core *core)
{
	platform_device_unregister(&core->sscd_pdev);
}

int vpu_do_sscoredump(struct vpu_core *core, const struct vpu_dump_info *dbg_info)
{
	struct sscd_platform_data *sscd_platdata;
	struct sscd_segment seg;

	dev_dbg(core->dev, "VPU core dump\n");
	sscd_platdata = dev_get_platdata(&core->sscd_pdev.dev);
	if (!sscd_platdata->sscd_report) {
		dev_dbg(core->dev, "No sscd_report\n");
		return -EINVAL;
	}

	if (!dbg_info) {
		dev_err(core->dev, "dbg_info is invalid\n");
		return -EINVAL;
	}

	memset(&seg, 0, sizeof(seg));
	seg.addr = dbg_info->addr;
	seg.size = dbg_info->size;
	if (dbg_info->crash_info[0] == '\0')
		snprintf(dbg_info->crash_info, VPU_CRASH_INFO_LEN, "VPU crash");

	return sscd_platdata->sscd_report(&core->sscd_pdev, &seg, 1,
		SSCD_FLAGS_ELFARM64HDR, dbg_info->crash_info);
}
