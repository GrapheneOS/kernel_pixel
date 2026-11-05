// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include "../../clk_defs.h"

#include <stddef.h>

/* LPCM IDs from private/google-modules/soc/rdo/include/dt-bindings/lpcm/pf_state_mbu.h */
#define LPCM_AOSS_PG 3
#define LPCM_LSIO_E 5
#define LPCM_LSIO_S 6
#define LPCM_HSIO_N 7
#define LPCM_HSIO_S 8
#define LPCM_PCIE 9
#define LPCM_CPU 17
#define LPCM_CPUACC 34
#define LPCM_DPU 35
#define LPCM_GPCM_AMB 41
#define LPCM_GPCM_INFRA 42

const char * const g_cpm_clk_power_domains[] = {
	"sswrp_dpu_pd",
	"dpu_be_pd",
	"dpu_fe0_pd",
	"dpu_fe1_pd",
	"sswrp_hsio_n_pd",
	"hsio_n_usb_pd",
	"hsio_n_usb_psw_pd",
	"hsio_n_ebu_pd",
	"hsio_n_dp_pd",
	"hsio_n_usb2aux_pd",
	"hsio_n_usb2aux_psw_pd",
	"sswrp_hsio_s_pd",
	"sswrp_lsio_s_pd",
	"sswrp_lsio_e_pd",
	"cpuacc_gpdma_pd",
	"sswrp_ispfe_pd",
	"ispfe_core0_pd",
	"ispfe_core1_pd",
	"ispfe_core2_pd",
	"ispfe_csis_pd",
	"sswrp_ispbe_pd",
	"sswrp_gcv_pd",
	"sswrp_pcie_pd",
	"pcie_top_pd",
	"hsio_s_ufs_pd",
	"hsio_s_ufs_prep_pd",
	"hsio_s_sd_pd",
	NULL
};

/* TODO(b/537184669): Investigate if any clocks in this blocklist can be unblocked. */
static const char * const g_dpu_state_blocklist[] = {
	"dpu_be_clk",
	"dpu_dp0_pix_clk",
	"dpu_dsi0_pix_clk",
	"dpu_dsi1_phy_clk",
	"dpu_dsi1_pix_clk",
	"dpu_fe0_clk",
	"dpu_fe1_clk",
	NULL
};

static const char * const g_hsio_s_state_blocklist[] = {
	"hsios_gpio_ref_clk",
	"hsios_mphy_ref_clk",
	NULL
};

static const char * const g_hsio_n_state_blocklist[] = {
	"hsion_usbdp_u3phy_fw_clk",
	"hsion_usbc_sticky_clk",
	"hsion_usbc_non_sticky_clk",
	"hsion_usb2_phy_reset_clk",
	"hsion_usb3dp_phy_reset_clk",
	"hsion_usbdp_u2phy_apb_clk",
	"hsion_usbdp_usbdrd_bus_clk",
	"hsion_usbdp_top_clk",
	"hsio_n_csr_clk",
	"hsio_n_maxi_clk",
	NULL
};

/* TODO(b/537308427): Investigate if any clocks in this blocklist can be unblocked. */
static const char * const g_aoss_pg_rate_blocklist[] = {
	"aoss_pg_amb_uart0_peri_clk",
	NULL
};

static const char * const g_dpu_rate_blocklist[] = {
	"dpu_be_clk",
	"dpu_dp0_pix_clk",
	"dpu_dsi0_pix_clk",
	"dpu_dsi1_phy_clk",
	"dpu_dsi1_pix_clk",
	"dpu_fe0_clk",
	"dpu_fe1_clk",
	NULL
};

static const char * const g_gpcm_amb_rate_blocklist[] = {
	"gpcm_hsion_pll_dp_clk",
	NULL
};

static const char * const g_hsio_n_rate_blocklist[] = {
	"hsion_usbdp_u3phy_fw_clk",
	"hsio_n_usbdp_sclk0_clk",
	"hsio_n_sresetn0_clk",
	NULL
};

static const char * const g_hsio_s_rate_blocklist[] = {
	"hsios_mphy_ref_clk",
	"hsios_ufs_clk",
	NULL
};

static const char * const g_lsio_s_rate_blocklist[] = {
	"lsio_s_m0_clk",
	"lsio_s_m1_clk",
	"lsio_s_m2_clk",
	"lsio_s_m3_clk",
	"lsio_s_m4_clk",
	"lsio_s_m5_clk",
	"lsio_s_m6_clk",
	"lsio_s_m7_clk",
	"lsio_s_pwm_clk",
	NULL
};

static const char * const g_gpcm_infra_rate_blocklist[] = {
	"gpcm_dpu_chnl_clk",
	"gpcm_ispbe_chnl_clk",
	"gpcm_ispfe_scan_wordclkhs_clk",
	"gpcm_gcv_chnl_clk",
	NULL
};

const struct clk_group_map g_cpm_clk_groups[] = {
	{ "aoss_pg", LPCM_AOSS_PG, NULL, g_aoss_pg_rate_blocklist },
	{ "cpu", LPCM_CPU, NULL, NULL },
	{ "cpuacc", LPCM_CPUACC, NULL, NULL },
	{ "dpu", LPCM_DPU, g_dpu_state_blocklist, g_dpu_rate_blocklist },
	{ "gpcm_amb", LPCM_GPCM_AMB, NULL, g_gpcm_amb_rate_blocklist },
	{ "gpcm_infra", LPCM_GPCM_INFRA, NULL, g_gpcm_infra_rate_blocklist },
	{ "hsio_n", LPCM_HSIO_N, g_hsio_n_state_blocklist, g_hsio_n_rate_blocklist },
	{ "hsio_s", LPCM_HSIO_S, g_hsio_s_state_blocklist, g_hsio_s_rate_blocklist },
	{ "lsio_e", LPCM_LSIO_E, NULL, NULL },
	{ "lsio_s", LPCM_LSIO_S, NULL, g_lsio_s_rate_blocklist },
	{ "pcie", LPCM_PCIE, NULL, NULL },
	{ NULL, 0, NULL, NULL }
};
