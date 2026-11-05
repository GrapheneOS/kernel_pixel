/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GS_THERMAL_H__
#define __GS_THERMAL_H__

#include <thermal_core.h>

static inline struct thermal_trip *gs_thermal_trip(struct thermal_zone_device *tz, int id)
{
	if (id < 0 || id >= tz->num_trips)
		return NULL;
	return &tz->trips[id].trip;
}

static inline int gs_thermal_get_trip_temp(struct thermal_zone_device *tz, int id, int *temp)
{
	struct thermal_trip *trip = gs_thermal_trip(tz, id);

	if (!trip)
		return -EINVAL;
	*temp = READ_ONCE(trip->temperature);
	return 0;
}

static inline int gs_thermal_get_trip_hyst(struct thermal_zone_device *tz, int id, int *hyst)
{
	struct thermal_trip *trip = gs_thermal_trip(tz, id);

	if (!trip)
		return -EINVAL;
	*hyst = READ_ONCE(trip->hysteresis);
	return 0;
}

static inline int gs_thermal_get_trip_type(struct thermal_zone_device *tz, int id,
					 enum thermal_trip_type *type)
{
	struct thermal_trip *trip = gs_thermal_trip(tz, id);

	if (!trip)
		return -EINVAL;
	*type = trip->type;
	return 0;
}

/* Called outside thermal callbacks, which already hold the zone lock. */
static inline int gs_thermal_set_trip_temp(struct thermal_zone_device *tz, int id, int temp)
{
	struct thermal_trip *trip = gs_thermal_trip(tz, id);
	int ret = 0;

	if (!trip)
		return -EINVAL;
	mutex_lock(&tz->lock);
	if (tz->ops.set_trip_temp)
		ret = tz->ops.set_trip_temp(tz, trip, temp);
	if (!ret)
		thermal_zone_set_trip_temp(tz, trip, temp);
	mutex_unlock(&tz->lock);
	return ret;
}

/* Flatten the per-trip lists while preserving normal loop break semantics. */
static inline struct thermal_instance *gs_thermal_next_instance(
	struct thermal_zone_device *tz, struct thermal_instance *prev)
{
	struct thermal_trip_desc *td = prev ? trip_to_trip_desc(prev->trip) : tz->trips;
	struct list_head *node;

	if (!tz->num_trips)
		return NULL;
	node = prev ? prev->trip_node.next : td->thermal_instances.next;

	for (; td < tz->trips + tz->num_trips; td++) {
		if (node != &td->thermal_instances)
			return list_entry(node, struct thermal_instance, trip_node);
		if (td + 1 < tz->trips + tz->num_trips)
			node = (td + 1)->thermal_instances.next;
	}
	return NULL;
}

#define gs_thermal_for_each_instance(instance, tz) \
	for (instance = gs_thermal_next_instance(tz, NULL); instance; \
	     instance = gs_thermal_next_instance(tz, instance))

static inline struct thermal_instance *gs_thermal_instance(
	struct thermal_zone_device *tz, struct thermal_cooling_device *cdev, int id)
{
	struct thermal_trip *trip = gs_thermal_trip(tz, id);
	struct thermal_instance *instance;

	if (!trip)
		return NULL;
	list_for_each_entry(instance, &trip_to_trip_desc(trip)->thermal_instances, trip_node)
		if (instance->cdev == cdev)
			return instance;
	return NULL;
}

static inline struct thermal_zone_device *gs_thermal_cdev_zone(
	struct thermal_cooling_device *cdev, const char *name)
{
	struct thermal_zone_device *tz = thermal_zone_get_zone_by_name(name);
	struct thermal_instance *instance;
	struct thermal_trip_desc *td;
	bool found = false;

	if (IS_ERR(tz))
		return NULL;
	mutex_lock(&cdev->lock);
	list_for_each_entry(instance, &cdev->thermal_instances, cdev_node) {
		for_each_trip_desc(tz, td)
			if (instance->trip == &td->trip)
				found = true;
	}
	mutex_unlock(&cdev->lock);
	return found ? tz : NULL;
}

#endif
