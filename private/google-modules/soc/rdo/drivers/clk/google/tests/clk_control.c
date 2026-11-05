// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include "clk_control.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

#include "bste_log.h"
#include "fs_utils.h"
#include "pd_control.h"

#include "clk_defs.h"

#define DEBUGFS_CLK "/d/clk"
#define DT_CPM_CLK "/proc/device-tree/cpm_clk"
#define CLK_PREPARE_ENABLE_FILE "clk_prepare_enable"
#define CLK_RATE_FILE "clk_rate"

static int clk_power_on_prereqs(void)
{
	int i;

	for (i = 0; g_cpm_clk_power_domains[i]; i++) {
		const char *dom = g_cpm_clk_power_domains[i];

		if (pd_get_debugfs_state(dom) == PD_STATE_ON)
			continue;

		BSTE_LOG_INFO("Powering ON prerequisite domain '%s'", dom);
		if (!pd_set_debugfs_state(dom, PD_STATE_ON)) {
			BSTE_LOG_ERR("Failed to power ON prerequisite domain '%s'", dom);
			return 0;
		}
	}
	return 1;
}

int clk_control_init(void)
{
	if (!fs_is_dir(DEBUGFS_CLK))
		mount("none", "/sys/kernel/debug", "debugfs", 0, NULL);

	if (!fs_is_dir(DEBUGFS_CLK)) {
		BSTE_LOG_ERR("Debugfs clock directory missing or not accessible: '%s'",
			     DEBUGFS_CLK);
		return 0;
	}

	if (!clk_power_on_prereqs())
		return 0;

	return 1;
}

int clk_scan_group_clocks(uint32_t target_lpcm_id, char clk_names[][MAX_CLK_NAME], int max_clocks)
{
	DIR *dir;
	struct dirent *ent;
	int count = 0;

	dir = opendir(DT_CPM_CLK);
	if (!dir) {
		BSTE_LOG_ERR("Failed opening directory '%s': %s (errno=%d)",
			     DT_CPM_CLK, strerror(errno), errno);
		return -1;
	}

	while ((ent = readdir(dir)) != NULL) {
		char path[FS_PATH_MAX];
		uint32_t lpcm_id;

		if (ent->d_name[0] == '.')
			continue;

		snprintf(path, sizeof(path), "%s/%s", DT_CPM_CLK, ent->d_name);
		if (!fs_is_dir(path))
			continue;

		snprintf(path, sizeof(path), "%s/%s/lpcm-id", DT_CPM_CLK, ent->d_name);
		if (!fs_read_u32_be(path, &lpcm_id)) {
			BSTE_LOG_ERR("Failed to read lpcm-id for clock node '%s'", ent->d_name);
			closedir(dir);
			return -1;
		}

		if (lpcm_id != target_lpcm_id)
			continue;

		if (count < max_clocks) {
			snprintf(clk_names[count], MAX_CLK_NAME, "%s", ent->d_name);
			count++;
		} else {
			BSTE_LOG_ERR("Exceeded maximum clock limit (%d) for group", max_clocks);
			closedir(dir);
			return -1;
		}
	}
	closedir(dir);
	return count;
}

int clk_set_prepare_enable(const char *clk_name, int enable)
{
	char sfile[FS_PATH_MAX];
	int current_state;
	int target = enable ? 1 : 0;

	if (!clk_name)
		return 0;

	if (clk_get_prepare_enable(clk_name, &current_state) && current_state == target)
		return 1;

	snprintf(sfile, sizeof(sfile), "%s/%s/%s", DEBUGFS_CLK, clk_name, CLK_PREPARE_ENABLE_FILE);
	return fs_write_text_file(sfile, target ? "1" : "0");
}

int clk_get_prepare_enable(const char *clk_name, int *enabled)
{
	char sfile[FS_PATH_MAX];
	char buf[64];
	ssize_t ret;

	if (!clk_name || !enabled)
		return 0;

	snprintf(sfile, sizeof(sfile), "%s/%s/%s", DEBUGFS_CLK, clk_name, CLK_PREPARE_ENABLE_FILE);
	ret = fs_read_text_file(sfile, buf, sizeof(buf));
	if (ret != 1) {
		BSTE_LOG_ERR("Clock '%s': clk_prepare_enable len %zd (expected 1)", clk_name, ret);
		return 0;
	}

	if (buf[0] == '1') {
		*enabled = 1;
		return 1;
	} else if (buf[0] == '0') {
		*enabled = 0;
		return 1;
	}

	BSTE_LOG_ERR("Clock '%s': unexpected content in clk_prepare_enable: '%s'", clk_name, buf);
	return 0;
}

int clk_get_rate(const char *clk_name, uint64_t *rate)
{
	char sfile[FS_PATH_MAX];
	char buf[64];
	char *endptr;
	ssize_t ret;
	unsigned long long val;

	if (!clk_name || !rate)
		return 0;

	snprintf(sfile, sizeof(sfile), "%s/%s/%s", DEBUGFS_CLK, clk_name, CLK_RATE_FILE);
	ret = fs_read_text_file(sfile, buf, sizeof(buf));
	if (ret <= 0) {
		BSTE_LOG_ERR("Clock '%s': failed to read clk_rate", clk_name);
		return 0;
	}

	errno = 0;
	val = strtoull(buf, &endptr, 10);
	if (errno != 0 || endptr == buf || (*endptr != '\0' && *endptr != '\n')) {
		BSTE_LOG_ERR("Clock '%s': failed to parse rate from string '%s'", clk_name, buf);
		return 0;
	}

	*rate = (uint64_t)val;
	return 1;
}

int clk_set_rate(const char *clk_name, uint64_t rate)
{
	char sfile[FS_PATH_MAX];
	char buf[64];

	if (!clk_name)
		return 0;

	snprintf(buf, sizeof(buf), "%llu", (unsigned long long)rate);
	snprintf(sfile, sizeof(sfile), "%s/%s/%s", DEBUGFS_CLK, clk_name, CLK_RATE_FILE);

	return fs_write_text_file(sfile, buf);
}

int clk_get_dt_rates(const char *clk_name, uint64_t rates[], int max_rates)
{
	char dt_path[FS_PATH_MAX];
	ssize_t ret;

	if (!clk_name || !rates || max_rates <= 0)
		return -1;

	snprintf(dt_path, sizeof(dt_path), "%s/%s/rates", DT_CPM_CLK, clk_name);
	if (!fs_file_exists(dt_path))
		return 0;

	ret = fs_read_u64_be_array(dt_path, rates, (size_t)max_rates);
	if (ret < 0) {
		BSTE_LOG_ERR("Clock '%s': failed to read DT rates array from '%s'",
			     clk_name, dt_path);
		return -1;
	}

	return (int)ret;
}

const struct clk_group_map *clk_find_group(const char *name)
{
	int i;

	if (!name)
		return NULL;

	for (i = 0; g_cpm_clk_groups[i].name; i++) {
		if (!strcmp(g_cpm_clk_groups[i].name, name))
			return &g_cpm_clk_groups[i];
	}
	return NULL;
}

static int is_clock_in_blocklist(const char * const *blocklist, const char *clock_name)
{
	int i;

	if (!blocklist || !clock_name)
		return 0;

	for (i = 0; blocklist[i]; i++) {
		if (!strcmp(blocklist[i], clock_name))
			return 1;
	}
	return 0;
}

int clk_is_clock_state_blocklisted(const struct clk_group_map *group, const char *clock_name)
{
	if (!group)
		return 0;
	return is_clock_in_blocklist(group->state_blocklist, clock_name);
}

int clk_is_clock_rate_blocklisted(const struct clk_group_map *group, const char *clock_name)
{
	if (!group)
		return 0;
	return is_clock_in_blocklist(group->rate_blocklist, clock_name);
}
