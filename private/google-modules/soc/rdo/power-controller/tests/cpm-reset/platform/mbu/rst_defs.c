// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include "../../rst_defs.h"
#include <stddef.h>

const struct rst_domain_info g_rst_domains[] = {
	{ "codec-3p-resets", "sswrp_codec_3p_pd" },
	{ "aoss-pg-amb-resets", "sswrp_aoss_pg_pd" },
	{ "lsio-s-resets", "sswrp_lsio_s_pd" },
	{ "lsio-e-resets", "sswrp_lsio_e_pd" },
	/* These domain names use underscores to match the naming in /d/cpm_rst. */
	{ "hsion_resets", "sswrp_hsio_n_pd" },
	{ "hsios_resets", "sswrp_hsio_s_pd" },
	{ "pcie-resets", "sswrp_pcie_pd" },
	{ NULL, NULL }
};
