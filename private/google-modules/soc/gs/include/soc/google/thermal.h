/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2025 Google LLC
 */

#ifndef __GOOGLE_THERMAL_H__
#define __GOOGLE_THERMAL_H__

#include <linux/thermal.h>

/* The core stores software hysteresis after this callback succeeds. */
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

	/* Preserve QPR2's writable DT trip hysteresis without a core hook. */
	if (IS_ENABLED(CONFIG_VH_THERMAL) && !zone_ops.set_trip_hyst)
		zone_ops.set_trip_hyst = google_thermal_set_trip_hyst;

	/* The thermal core copies the operations into the zone. */
	return devm_thermal_of_zone_register(dev, id, data, &zone_ops);
}

#if IS_ENABLED(CONFIG_VH_THERMAL)
void register_tz_id_ignore_genl(int tz_id);
void register_tz_id_irq_wakeable(int tz_id);
#else
static inline void register_tz_id_ignore_genl(int tz_id) {}
static inline void register_tz_id_irq_wakeable(int tz_id)
{
}
#endif /* CONFIG_VH_THERMAL */

#endif /* __GOOGLE_THERMAL_H__ */
