// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include "rst_control.h"
#include "../common/fs_utils.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>

#define DEBUGFS_CPM_RST "/d/cpm_rst"
#define DT_CPM_RST "/proc/device-tree/cpm_rst"

int rst_control_init(void)
{
	if (!fs_is_dir(DEBUGFS_CPM_RST))
		mount("none", "/sys/kernel/debug", "debugfs", 0, NULL);

	if (!fs_is_dir(DEBUGFS_CPM_RST)) {
		fprintf(stderr, "ERROR: Debugfs directory missing: %s\n",
			DEBUGFS_CPM_RST);
		return 0;
	}
	return 1;
}

const struct rst_domain_info *rst_find_domain(const char *name)
{
	int i;

	if (!name)
		return NULL;

	for (i = 0; g_rst_domains[i].name != NULL; i++) {
		if (!strcmp(g_rst_domains[i].name, name))
			return &g_rst_domains[i];
	}
	return NULL;
}

int rst_get_reset_num(const char *domain_name)
{
	char path[512];
	uint32_t val;

	if (!domain_name)
		return -1;

	snprintf(path, sizeof(path), "%s/%s/reset-num", DT_CPM_RST, domain_name);
	if (!fs_read_u32_be(path, &val)) {
		fprintf(stderr, "ERROR: Failed to read reset-num from %s\n", path);
		return -1;
	}

	return (int)val;
}

static int rst_write_action(const char *domain_name, const char *action, int rst_id)
{
	char path[512];
	char id_str[32];

	if (!domain_name || !action || rst_id < 0)
		return 0;

	snprintf(path, sizeof(path), "%s/%s/%s", DEBUGFS_CPM_RST, domain_name, action);
	snprintf(id_str, sizeof(id_str), "%d\n", rst_id);
	return fs_write_text_file(path, id_str);
}

int rst_assert(const char *domain_name, int rst_id)
{
	return rst_write_action(domain_name, "assert", rst_id);
}

int rst_deassert(const char *domain_name, int rst_id)
{
	return rst_write_action(domain_name, "deassert", rst_id);
}
