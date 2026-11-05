/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GOOGLE_THERMAL_H__
#define __GOOGLE_THERMAL_H__

#include <linux/thermal.h>

/* The thermal core stores software hysteresis after this callback succeeds. */
static inline int google_thermal_set_trip_hyst(struct thermal_zone_device *tz,
					      const struct thermal_trip *trip, int hyst)
{
	return 0;
}

static inline struct thermal_zone_device *google_devm_thermal_of_zone_register(
		struct device *dev, int id, void *data,
		const struct thermal_zone_device_ops *ops)
{
	struct thermal_zone_device_ops zone_ops = *ops;

	/* Preserve the default writable DT trip hysteresis provided by 6.1. */
	if (!zone_ops.set_trip_hyst)
		zone_ops.set_trip_hyst = google_thermal_set_trip_hyst;

	/* The thermal core copies the operations into the zone. */
	return devm_thermal_of_zone_register(dev, id, data, &zone_ops);
}

#endif
