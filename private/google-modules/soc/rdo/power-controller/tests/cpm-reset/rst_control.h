/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2026 Google LLC */

#ifndef RST_CONTROL_H
#define RST_CONTROL_H

#include "rst_defs.h"

/**
 * rst_control_init - Initialize reset control interface and ensure debugfs is mounted.
 *
 * Return: 1 on success, 0 on failure.
 */
int rst_control_init(void);

/**
 * rst_find_domain - Look up platform reset domain descriptor by name.
 * @name: Reset domain name.
 *
 * Return: Pointer to struct rst_domain_info on match, or NULL if not found.
 */
const struct rst_domain_info *rst_find_domain(const char *name);

/**
 * rst_get_reset_num - Read the number of reset IDs for a reset domain.
 * @domain_name: Reset domain name (e.g. "codec-3p-resets").
 *
 * Return: Number of reset IDs (reset_num > 0) on success, or -1 on failure.
 */
int rst_get_reset_num(const char *domain_name);

/**
 * rst_assert - Assert a reset ID on a reset domain.
 * @domain_name: Reset domain name.
 * @rst_id: Reset ID (0 to reset_num - 1).
 *
 * Return: 1 on success, 0 on failure.
 */
int rst_assert(const char *domain_name, int rst_id);

/**
 * rst_deassert - Deassert a reset ID on a reset domain.
 * @domain_name: Reset domain name.
 * @rst_id: Reset ID (0 to reset_num - 1).
 *
 * Return: 1 on success, 0 on failure.
 */
int rst_deassert(const char *domain_name, int rst_id);

#endif /* RST_CONTROL_H */
