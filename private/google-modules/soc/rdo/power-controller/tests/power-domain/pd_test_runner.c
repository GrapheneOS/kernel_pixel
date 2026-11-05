// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "bste_log.h"

#include "pd_control.h"
#include "pd_defs.h"

#define PD_TEST_SUITE_START_FMT "[PD_TEST] SUITE=%s ACTION=START COUNT=%d"
#define PD_TEST_STEP_PASS_FMT   "[PD_TEST] PHASE=%s DOMAIN=%s STATUS=OK"
#define PD_TEST_STEP_FAIL_FMT   "[PD_TEST] PHASE=%s DOMAIN=%s STATUS=FAIL REASON=%s ERRNO=%d"
#define PD_TEST_SUITE_END_FMT   "[PD_TEST] SUITE=%s ACTION=END STATUS=%s PHASE_1=%d PHASE_2=%d PHASE_3=%d"

#define PD_TEST_REASON_WRITE_FAIL    "WRITE_FAIL"
#define PD_TEST_REASON_DFS_MISMATCH  "DEBUGFS_MISMATCH"
#define PD_TEST_REASON_MBFS_MISMATCH "MBFS_MISMATCH"

struct pd_test_result {
	const char *suite;
	int is_pass;
	int phase1_pass;
	int phase2_pass;
	int phase3_pass;
};

static void pd_log_suite_start(const char *suite, int count)
{
	BSTE_LOG_INFO(PD_TEST_SUITE_START_FMT, suite, count);
}

static void pd_log_step_pass(const char *phase, const char *domain)
{
	BSTE_LOG_INFO(PD_TEST_STEP_PASS_FMT, phase, domain);
}

static void pd_log_step_fail(const char *phase, const char *domain,
			     const char *reason, int err_code)
{
	BSTE_LOG_ERR(PD_TEST_STEP_FAIL_FMT, phase, domain, reason, err_code);
}

static void pd_log_suite_end(const struct pd_test_result *res)
{
	const char *status_str = res->is_pass ? "PASS" : "FAIL";

	if (res->is_pass)
		BSTE_LOG_INFO(PD_TEST_SUITE_END_FMT, res->suite, status_str,
			      res->phase1_pass, res->phase2_pass, res->phase3_pass);
	else
		BSTE_LOG_ERR(PD_TEST_SUITE_END_FMT, res->suite, status_str,
			     res->phase1_pass, res->phase2_pass, res->phase3_pass);
}

static struct power_domain *g_power_on_sequence[MAX_DOMAINS];
static int g_power_on_count;
static struct power_domain *g_power_off_sequence[MAX_DOMAINS];
static int g_power_off_count;

static void get_power_on_sequence(struct power_domain *curr)
{
	if (!curr)
		return;

	for (int i = 0; i < g_power_on_count; i++) {
		if (g_power_on_sequence[i] == curr)
			return;
	}

	if (g_power_on_count < MAX_DOMAINS)
		g_power_on_sequence[g_power_on_count++] = curr;

	for (int i = 0; i < curr->child_count; i++)
		get_power_on_sequence(curr->children[i]);
}

static void get_power_off_sequence(struct power_domain *curr)
{
	if (!curr)
		return;

	for (int i = 0; i < curr->child_count; i++)
		get_power_off_sequence(curr->children[i]);

	if (curr->is_always_on)
		return;

	for (int i = 0; i < g_power_off_count; i++) {
		if (g_power_off_sequence[i] == curr)
			return;
	}
	if (g_power_off_count < MAX_DOMAINS)
		g_power_off_sequence[g_power_off_count++] = curr;
}

static int verify_domain(struct power_domain *domain, enum pd_state expected_state,
			 const char *phase_str)
{
	const char *expected_str;
	enum pd_state cur_state;
	enum pd_state actual_mbfs;

	if (!domain || expected_state < 0) {
		BSTE_LOG_ERR("%s: invalid domain pointer or state", __func__);
		return 0;
	}

	expected_str = g_pd_state_strings[expected_state];
	if (!expected_str) {
		BSTE_LOG_ERR("No state string mapping found for state enum %d", expected_state);
		return 0;
	}

	BSTE_LOG_INFO("Setting domain '%s' state to %s", domain->name, expected_str);

	if (!pd_set_debugfs_state(domain->name, expected_state)) {
		pd_log_step_fail(phase_str, domain->name, PD_TEST_REASON_WRITE_FAIL, errno);
		return 0;
	}

	cur_state = pd_get_debugfs_state(domain->name);
	if (cur_state != expected_state) {
		pd_log_step_fail(phase_str, domain->name, PD_TEST_REASON_DFS_MISMATCH, 0);
		return 0;
	}

	actual_mbfs = pd_get_mbfs_state(domain->mbfs_name);
	if (actual_mbfs != expected_state) {
		pd_log_step_fail(phase_str, domain->name, PD_TEST_REASON_MBFS_MISMATCH, 0);
		return 0;
	}

	pd_log_step_pass(phase_str, domain->name);
	return 1;
}

static int verify_subsystem(const char *root_name)
{
	struct power_domain *root;
	struct pd_test_result res = { 0 };

	if (!root_name) {
		BSTE_LOG_ERR("%s: root_name is NULL", __func__);
		return 0;
	}

	root = pd_find_domain(root_name);
	if (!root) {
		BSTE_LOG_ERR("Root domain '%s' not found in power_controller tree", root_name);
		return 0;
	}

	g_power_on_count = 0;
	get_power_on_sequence(root);

	g_power_off_count = 0;
	get_power_off_sequence(root);

	if (g_power_on_count == 0) {
		BSTE_LOG_ERR("No testable ON domains resolved for root '%s'", root_name);
		return 0;
	}

	res.suite = root_name;
	res.phase1_pass = 1;
	res.phase2_pass = 1;
	res.phase3_pass = 1;

	pd_log_suite_start(root_name, g_power_on_count);

	BSTE_LOG_INFO("Powering ON subtree for '%s' (count=%d)", root_name, g_power_on_count);
	for (int i = 0; i < g_power_on_count; i++) {
		if (!verify_domain(g_power_on_sequence[i], PD_STATE_ON, "1_ENABLE")) {
			res.phase1_pass = 0;
			BSTE_LOG_ERR("Phase 1 enablement aborted on domain '%s'",
				     g_power_on_sequence[i]->name);
			break;
		}
	}

	BSTE_LOG_INFO("Powering OFF subtree for '%s' (count=%d)", root_name, g_power_off_count);
	for (int i = 0; i < g_power_off_count; i++) {
		if (!verify_domain(g_power_off_sequence[i], PD_STATE_OFF, "2_DISABLE")) {
			res.phase2_pass = 0;
			BSTE_LOG_ERR("Phase 2 disablement aborted on domain '%s'",
				     g_power_off_sequence[i]->name);
			break;
		}
	}

	BSTE_LOG_INFO("Restoring (Powering ON) subtree for '%s' (count=%d)",
		      root_name, g_power_on_count);
	for (int i = 0; i < g_power_on_count; i++) {
		if (!verify_domain(g_power_on_sequence[i], PD_STATE_ON, "3_RESTORE")) {
			res.phase3_pass = 0;
			BSTE_LOG_ERR("Phase 3 restoration aborted on domain '%s'",
				     g_power_on_sequence[i]->name);
			break;
		}
	}

	res.is_pass = (res.phase1_pass && res.phase2_pass && res.phase3_pass) ? 1 : 0;
	pd_log_suite_end(&res);
	return res.is_pass;
}

static int is_valid_sswrp_domain(const char *name)
{
	size_t len;

	if (!name)
		return 0;

	len = strlen(name);
	if (!strncmp(name, "sswrp", 5))
		return 1;
	if (len >= 3 && !strcmp(name + len - 3, "_pd"))
		return 1;

	return 0;
}

int main(int argc, char *argv[])
{
	const char *root_name;

	if (argc < 2) {
		BSTE_LOG_ERR("Usage: %s <domain_name>", argv[0]);
		return 1;
	}

	if (!is_valid_sswrp_domain(argv[1])) {
		BSTE_LOG_ERR("Invalid power domain target: '%s'", argv[1]);
		return 1;
	}
	root_name = argv[1];

	if (!pd_control_init(root_name))
		return 1;

	if (!verify_subsystem(root_name))
		return 1;

	return 0;
}
