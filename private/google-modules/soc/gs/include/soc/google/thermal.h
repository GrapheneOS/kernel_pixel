/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2025 Google LLC
 */

#ifndef __GOOGLE_THERMAL_H__
#define __GOOGLE_THERMAL_H__

#include <linux/thermal.h>

static inline struct thermal_zone_device *google_devm_thermal_of_zone_register(
		struct device *dev, int id, void *data,
		const struct thermal_zone_device_ops *ops)
{
	struct thermal_zone_device_ops zone_ops = *ops;

	zone_ops.writable_trip_hyst = IS_ENABLED(CONFIG_VH_THERMAL);

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
