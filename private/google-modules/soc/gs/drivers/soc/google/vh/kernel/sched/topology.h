/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/types.h>

struct cpumask;

void vh_arch_set_freq_scale_pixel_mod(void *data,
					     const struct cpumask *cpus,
					     unsigned long freq,
					     unsigned long max,
					     unsigned long *scale);
void android_vh_use_amu_fie_pixel_mod(void* data, bool *use_amu_fie);
