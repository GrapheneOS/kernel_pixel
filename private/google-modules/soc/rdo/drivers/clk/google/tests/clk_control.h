/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2026 Google LLC */

#ifndef CLK_CONTROL_H
#define CLK_CONTROL_H

#include <stdint.h>

#define MAX_GROUP_CLOCKS 128
#define MAX_CLK_NAME 64
#define MAX_CLK_RATES 64

struct clk_group_map;

/*
 * Ensures debugfs is mounted and accessible for clock testing.
 * Returns 1 on success, 0 on failure.
 */
int clk_control_init(void);

/*
 * Scans /proc/device-tree/cpm_clk for clocks matching target_lpcm_id.
 * Populates clk_names array and returns number of clocks found, or -1 on error.
 */
int clk_scan_group_clocks(uint32_t target_lpcm_id, char clk_names[][MAX_CLK_NAME], int max_clocks);

/* Looks up platform clock group mapping by name. Returns pointer to map on match, or NULL. */
const struct clk_group_map *clk_find_group(const char *name);

/* Returns 1 if clock_name is in the group's state_blocklist, 0 otherwise. */
int clk_is_clock_state_blocklisted(const struct clk_group_map *group, const char *clock_name);

/* Returns 1 if clock_name is in the group's rate_blocklist, 0 otherwise. */
int clk_is_clock_rate_blocklisted(const struct clk_group_map *group, const char *clock_name);

/*
 * Writes "1" (if enable!=0) or "0" to /d/clk/<clk_name>/clk_prepare_enable.
 * Returns 1 on success, 0 on error.
 */
int clk_set_prepare_enable(const char *clk_name, int enable);

/*
 * Reads /d/clk/<clk_name>/clk_prepare_enable and stores the integer value (0 or 1) in *enabled.
 * Returns 1 on successful read and parse, 0 on failure.
 */
int clk_get_prepare_enable(const char *clk_name, int *enabled);

/*
 * Reads /d/clk/<clk_name>/clk_rate and stores the rate in *rate.
 * Returns 1 on success, 0 on error.
 */
int clk_get_rate(const char *clk_name, uint64_t *rate);

/* Writes rate string to /d/clk/<clk_name>/clk_rate. Returns 1 on success, 0 on error. */
int clk_set_rate(const char *clk_name, uint64_t rate);

/*
 * Reads supported rates array from /proc/device-tree/cpm_clk/<clk_name>/rates.
 * Returns number of rates read (>= 0), or -1 on error (e.g. file read failure).
 */
int clk_get_dt_rates(const char *clk_name, uint64_t rates[], int max_rates);

#endif /* CLK_CONTROL_H */
