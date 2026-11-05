/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/types.h>

struct task_struct;
struct cgroup;

void rvh_cgroup_force_kthread_migration_pixel_mod(void *data, struct task_struct *tsk,
						  struct cgroup *dst_cgrp, bool *force_migration);
