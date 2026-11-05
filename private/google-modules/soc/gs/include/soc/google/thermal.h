/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GOOGLE_THERMAL_H__
#define __GOOGLE_THERMAL_H__

#include <linux/thermal.h>

static inline struct thermal_zone_device *google_devm_thermal_of_zone_register(
		struct device *dev, int id, void *data,
		const struct thermal_zone_device_ops *ops)
{
	struct thermal_zone_device_ops zone_ops = *ops;

	/* Preserve the default writable DT trip hysteresis provided by 6.1. */
	zone_ops.writable_trip_hyst = true;

	/* The thermal core copies the operations into the zone. */
	return devm_thermal_of_zone_register(dev, id, data, &zone_ops);
}

#endif
