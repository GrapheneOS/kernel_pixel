/* SPDX-License-Identifier: GPL-2.0-only */

struct kobject;

int thermal_metrics_init(struct kobject *metrics_kobj);
int perf_metrics_init(struct kobject *metrics_kobj);
