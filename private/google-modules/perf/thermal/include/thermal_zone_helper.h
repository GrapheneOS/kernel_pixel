// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_zone_helper.h API's to register, unregister and handle
 * thermal zone(s).
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_ZONE_HELPER_H_
#define _THERMAL_ZONE_HELPER_H_

#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <soc/google/thermal_metrics.h>

#include "thermal_core.h"

/* The caller holds the zone lock, or owns an unpublished test zone. */
static inline int google_thermal_get_trip(struct thermal_zone_device *tz, int id,
					 struct thermal_trip *trip)
{
	if (id < 0 || id >= tz->num_trips)
		return -EINVAL;
	*trip = tz->trips[id].trip;
	return 0;
}

static inline int google_thermal_get_trip_locked(struct thermal_zone_device *tz, int id,
						struct thermal_trip *trip)
{
	guard(thermal_zone)(tz);
	return google_thermal_get_trip(tz, id, trip);
}

static inline int google_thermal_trip_id(struct thermal_zone_device *tz,
					 const struct thermal_trip *trip)
{
	return trip_to_trip_desc(trip) - tz->trips;
}

#define TEMPERATURE_LOG_BUF_LEN	5

struct sensor_temp_log {
	ktime_t				time;
	int				temperature;
	int				reg;
	int				read_ct;
};

struct ntc_sensor_data {
	int				low_trip;
	int				high_trip;
	int				fault_trip;
	tr_handle			tr_stats_handle;
	int				log_ct;
	struct sensor_temp_log		temp_log[TEMPERATURE_LOG_BUF_LEN];
	bool				irq_handle_phase;
	int				irq_temp;
};

union sensor_data {
	struct ntc_sensor_data		ntc_data;
};

struct google_sensor_data {
	struct device			*dev;
	struct thermal_zone_device	*tzd;
	int				sensor_id;
	union sensor_data		sens_data;
	void				*sens_priv_data;
	struct list_head		node;
};

typedef int (set_fault_trip_handler)(struct thermal_zone_device *, int);

#if IS_ENABLED(CONFIG_KUNIT)
void tzh_register_tz_id_irq_wakeable(int tz_id);
#endif // CONFIG_KUNIT

int thermal_register_sensors(struct device *dev, int sensor_ct,
			     const struct thermal_zone_device_ops *ops, struct list_head *head,
			     bool ignore_genl, bool irq_wakeable);
int thermal_unregister_sensors(struct device *dev, struct list_head *head);

int thermal_fetch_and_register_irq(struct platform_device *pdev,
				   irq_handler_t handler, void *data,
				   unsigned long irqflags);

int thermal_setup_irq_and_sensors(struct platform_device *pdev, int sensor_ct,
				  const struct thermal_zone_device_ops *ops,
				  struct list_head *head, irq_handler_t handler,
				  unsigned long irqflags);

int thermal_configure_fault_threshold(struct list_head *head,
				      set_fault_trip_handler fault_trip);

#endif  // _THERMAL_ZONE_HELPER_H_
