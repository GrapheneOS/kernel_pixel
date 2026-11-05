/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Define configuration macros.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#ifndef __GCIP_CONFIG_H__
#define __GCIP_CONFIG_H__

#include <linux/kconfig.h>

/* Macros to check the availability of features and APIs */

#define GCIP_IOMMU_MAP_HAS_GFP 1

#ifndef GCIP_ASSERT_ENABLE
#define GCIP_ASSERT_ENABLE IS_ENABLED(CONFIG_GCIP_UNIT)
#endif

#endif /* __GCIP_CONFIG_H__ */
