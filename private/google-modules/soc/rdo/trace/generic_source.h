/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */

#ifndef GENERIC_SOURCE_H_
#define GENERIC_SOURCE_H_

#include <linux/coresight.h>

struct soc_source_drvdata {
	struct coresight_device *csdev;
	u8 traceid;
};

#endif  // GENERIC_SOURCE_H_
