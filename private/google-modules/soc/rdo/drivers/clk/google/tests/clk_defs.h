/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2026 Google LLC */

#ifndef CLK_DEFS_H
#define CLK_DEFS_H

#include <stdint.h>

struct clk_group_map {
	const char *name;
	uint32_t lpcm_id;
	const char * const *state_blocklist;
	const char * const *rate_blocklist;
};

extern const char * const g_cpm_clk_power_domains[];
extern const struct clk_group_map g_cpm_clk_groups[];

#endif /* CLK_DEFS_H */
