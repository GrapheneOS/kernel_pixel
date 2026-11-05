// SPDX-License-Identifier: GPL-2.0-only
/* init.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2021 Google LLC
 */

#include <linux/module.h>
#include <trace/hooks/thermal.h>

#include <soc/google/thermal.h>

#include "thermal_genl.h"

static void vh_update_thermal_trip_flag(void *data, struct thermal_trip *trip)
{
	trip->flags |= THERMAL_TRIP_FLAG_RW_HYST;
}

/* Make sensor modules depend on the trip hook before registering a zone. */
struct thermal_zone_device *google_devm_thermal_of_zone_register(
		struct device *dev, int id, void *data,
		const struct thermal_zone_device_ops *ops)
{
	return devm_thermal_of_zone_register(dev, id, data, ops);
}
EXPORT_SYMBOL_GPL(google_devm_thermal_of_zone_register);

static int vh_thermal_init(void)
{
	int ret;

	ret = register_trace_android_vh_enable_thermal_genl_check(
			vh_enable_thermal_genl_check, NULL);
	if (ret)
		return ret;

	ret = register_trace_android_vh_update_thermal_trip_flag(
			vh_update_thermal_trip_flag, NULL);
	if (ret) {
		unregister_trace_android_vh_enable_thermal_genl_check(
				vh_enable_thermal_genl_check, NULL);
		tracepoint_synchronize_unregister();
	}

	return ret;
}

module_init(vh_thermal_init);
MODULE_LICENSE("GPL v2");
