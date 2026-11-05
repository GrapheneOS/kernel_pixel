/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GOOGLE_THERMAL_H__
#define __GOOGLE_THERMAL_H__

#include <linux/thermal.h>

#if IS_REACHABLE(CONFIG_VH_THERMAL)
struct thermal_zone_device *google_devm_thermal_of_zone_register(
		struct device *dev, int id, void *data,
		const struct thermal_zone_device_ops *ops);
#else
#define google_devm_thermal_of_zone_register devm_thermal_of_zone_register
#endif

#endif
