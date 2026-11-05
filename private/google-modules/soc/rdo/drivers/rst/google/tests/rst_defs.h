/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2026 Google LLC */

#ifndef RST_DEFS_H
#define RST_DEFS_H

/**
 * struct rst_domain_info - Platform-specific reset domain descriptor.
 * @name: Devicetree and DebugFS domain node name (e.g. "codec-3p-resets").
 * @required_pd: Top-level prerequisite root power domain name.
 */
struct rst_domain_info {
	const char *name;
	const char *required_pd;
};

/**
 * g_rst_domains - Null-terminated array of platform reset domains.
 */
extern const struct rst_domain_info g_rst_domains[];

#endif /* RST_DEFS_H */
