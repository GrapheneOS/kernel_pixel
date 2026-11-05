// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <dt-bindings/lpcm/pf_state_mbu.h>
#include <dt-bindings/lpcm/reset_mbu.h>

#include "rst-cpm.h"

#define PERI_MAP(_id, _clk, _cfg_clk) \
	{ .id = (_id), .clk_id = (_clk), .cfg_clk_id = (_cfg_clk) }

static const struct cpm_peri_rst_map lsio_s_peri_map[] = {
	PERI_MAP(RST_LSIO_S_I2C_0_PERI_CFG_CLK, RST_LSIO_S_CG_I2C_0_CLK,
		 RST_LSIO_S_CG_I2C_0_CFG_CLK),
	PERI_MAP(RST_LSIO_S_I2C_1_PERI_CFG_CLK, RST_LSIO_S_CG_I2C_1_CLK,
		 RST_LSIO_S_CG_I2C_1_CFG_CLK),
	PERI_MAP(RST_LSIO_S_I2C_2_PERI_CFG_CLK, RST_LSIO_S_CG_I2C_2_CLK,
		 RST_LSIO_S_CG_I2C_2_CFG_CLK),
	PERI_MAP(RST_LSIO_S_I2C_3_PERI_CFG_CLK, RST_LSIO_S_CG_I2C_3_CLK,
		 RST_LSIO_S_CG_I2C_3_CFG_CLK),
	PERI_MAP(RST_LSIO_S_I2C_4_PERI_CFG_CLK, RST_LSIO_S_CG_I2C_4_CLK,
		 RST_LSIO_S_CG_I2C_4_CFG_CLK),
};

static const struct cpm_peri_rst_map lsio_e_peri_map[] = {
	PERI_MAP(RST_LSIO_E_I2C_0_PERI_CFG_CLK, RST_LSIO_E_CG_I2C_0_CLK,
		 RST_LSIO_E_CG_I2C_0_CFG_CLK),
	PERI_MAP(RST_LSIO_E_I2C_1_PERI_CFG_CLK, RST_LSIO_E_CG_I2C_1_CLK,
		 RST_LSIO_E_CG_I2C_1_CFG_CLK),
	PERI_MAP(RST_LSIO_E_I2C_2_PERI_CFG_CLK, RST_LSIO_E_CG_I2C_2_CLK,
		 RST_LSIO_E_CG_I2C_2_CFG_CLK),
	PERI_MAP(RST_LSIO_E_I2C_3_PERI_CFG_CLK, RST_LSIO_E_CG_I2C_3_CLK,
		 RST_LSIO_E_CG_I2C_3_CFG_CLK),
	PERI_MAP(RST_LSIO_E_I2C_4_PERI_CFG_CLK, RST_LSIO_E_CG_I2C_4_CLK,
		 RST_LSIO_E_CG_I2C_4_CFG_CLK),
};

const struct cpm_peri_rst_map *goog_cpm_rst_find_peri_map(u32 lpcm_id, unsigned long id)
{
	const struct cpm_peri_rst_map *map = NULL;
	size_t count = 0;
	size_t i;

	switch (lpcm_id) {
	case LPCM_LSIO_S:
		map = lsio_s_peri_map;
		count = ARRAY_SIZE(lsio_s_peri_map);
		break;
	case LPCM_LSIO_E:
		map = lsio_e_peri_map;
		count = ARRAY_SIZE(lsio_e_peri_map);
		break;
	default:
		break;
	}

	if (!map)
		return NULL;

	for (i = 0; i < count; i++) {
		if (map[i].id == id)
			return &map[i];
	}

	return NULL;
}
