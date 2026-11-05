// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bste_log.h"
#include "kmsg_utils.h"

#include "clk_control.h"
#include "clk_defs.h"

#define TAG_CLK_STATE_TEST "CLK_STATE_TEST"
#define TAG_CLK_RATE_TEST  "CLK_RATE_TEST"

#define VAL_UNKNOWN (-1LL)

#define CLK_TEST_SUITE_START_FMT "[%s] SUITE=%s ACTION=START COUNT=%d"
#define CLK_TEST_STEP_PASS_FMT   "[%s] DOMAIN=%s CLOCK=%s ACTION=%s STATUS=OK"
#define CLK_TEST_STEP_SKIP_FMT   "[%s] DOMAIN=%s CLOCK=%s ACTION=%s STATUS=SKIP"
#define CLK_TEST_STEP_FAIL_FMT   "[%s] DOMAIN=%s CLOCK=%s ACTION=%s TARGET=%lld ACTUAL=%lld STATUS=FAIL REASON=%s ERRNO=%d"
#define CLK_TEST_SUITE_END_FMT   "[%s] SUITE=%s ACTION=END STATUS=%s SUCCESS_CLK_NUM=%d FAIL_CLK_NUM=%d SKIP_CLK_NUM=%d KMSG_PASS=%d"

#define CLK_TEST_ACT_READ_INITIAL          "READ_INITIAL"
#define CLK_TEST_ACT_RESTORE_ENABLE        "RESTORE_ENABLE"

#define CLK_STATE_TEST_ACT_ENABLE_1        "ENABLE_1"
#define CLK_STATE_TEST_ACT_DISABLE_2       "DISABLE_2"
#define CLK_STATE_TEST_ACT_ENABLE_3        "ENABLE_3"

#define CLK_RATE_TEST_ACT_PREPARE_ENABLE   "PREPARE_ENABLE"
#define CLK_RATE_TEST_ACT_GET_DT_RATES     "GET_DT_RATES"
#define CLK_RATE_TEST_ACT_GET_INIT_RATE    "GET_INIT_RATE"
#define CLK_RATE_TEST_ACT_SET_RATE         "SET_RATE"
#define CLK_RATE_TEST_ACT_RESTORE_RATE     "RESTORE_RATE"

#define CLK_TEST_REASON_WRITE_FAIL         "WRITE_FAIL"
#define CLK_TEST_REASON_READ_FAIL          "READ_FAIL"
#define CLK_TEST_REASON_VERIFY_FAIL        "VERIFY_FAIL"

typedef int (*clk_test_fn)(const struct clk_group_map *group,
			   char clk_names[][MAX_CLK_NAME],
			   int clk_count, int kmsg_fd);

enum clk_test_res {
	CLK_RES_FAIL = 0,
	CLK_RES_PASS = 1,
	CLK_RES_SKIP = 2,
};

struct clk_test_result {
	const char *suite;
	int is_pass;
	int success_clk_num;
	int fail_clk_num;
	int skip_clk_num;
	int kmsg_pass;
};

static void clk_log_suite_start(const char *tag, const char *suite, int count)
{
	BSTE_LOG_INFO(CLK_TEST_SUITE_START_FMT, tag, suite, count);
}

static void clk_log_step_pass(const char *tag, const char *domain,
			      const char *clock, const char *action)
{
	BSTE_LOG_INFO(CLK_TEST_STEP_PASS_FMT, tag, domain, clock, action);
}

static void clk_log_step_skip(const char *tag, const char *domain,
			      const char *clock, const char *action)
{
	BSTE_LOG_INFO(CLK_TEST_STEP_SKIP_FMT, tag, domain, clock, action);
}

static void clk_log_step_fail(const char *tag, const char *domain, const char *clock,
			      const char *action, int64_t target_val, int64_t actual_val,
			      const char *reason, int err_code)
{
	BSTE_LOG_ERR(CLK_TEST_STEP_FAIL_FMT, tag, domain, clock, action,
		     (long long)target_val,
		     (long long)actual_val,
		     reason, err_code);
}

static void clk_log_suite_end(const char *tag, const struct clk_test_result *res)
{
	const char *status_str = res->is_pass ? "PASS" : "FAIL";

	if (res->is_pass)
		BSTE_LOG_INFO(CLK_TEST_SUITE_END_FMT, tag, res->suite, status_str,
			      res->success_clk_num, res->fail_clk_num,
			      res->skip_clk_num, res->kmsg_pass);
	else
		BSTE_LOG_ERR(CLK_TEST_SUITE_END_FMT, tag, res->suite, status_str,
			     res->success_clk_num, res->fail_clk_num,
			     res->skip_clk_num, res->kmsg_pass);
}

static int verify_clock_state(const char *domain_name, const char *cname,
			      int target_state, const char *action_str,
			      int index, int total)
{
	int enabled;

	BSTE_LOG_INFO("Action %s for clock '%s' (%d/%d)",
		      action_str, cname, index + 1, total);

	if (!clk_set_prepare_enable(cname, target_state)) {
		clk_log_step_fail(TAG_CLK_STATE_TEST, domain_name, cname,
				  action_str, target_state, VAL_UNKNOWN,
				  CLK_TEST_REASON_WRITE_FAIL, errno);
		return 0;
	}

	if (!clk_get_prepare_enable(cname, &enabled)) {
		clk_log_step_fail(TAG_CLK_STATE_TEST, domain_name, cname,
				  action_str, target_state, VAL_UNKNOWN,
				  CLK_TEST_REASON_READ_FAIL, errno);
		return 0;
	}

	if (enabled != target_state) {
		clk_log_step_fail(TAG_CLK_STATE_TEST, domain_name, cname,
				  action_str, target_state, enabled,
				  CLK_TEST_REASON_VERIFY_FAIL, 0);
		return 0;
	}

	clk_log_step_pass(TAG_CLK_STATE_TEST, domain_name, cname, action_str);
	return 1;
}

static int run_verify_state_test(const struct clk_group_map *group,
				 char clk_names[][MAX_CLK_NAME],
				 int clk_count, int kmsg_fd)
{
	struct clk_test_result res = { 0 };
	int i;

	res.suite = group->name;
	res.kmsg_pass = 1;

	clk_log_suite_start(TAG_CLK_STATE_TEST, group->name, clk_count);

	BSTE_LOG_INFO("Verifying state transitions for %d clocks in domain '%s'",
		      clk_count, group->name);

	for (i = 0; i < clk_count; i++) {
		int initial_enable = 0;
		int is_clock_ok = 1;

		if (clk_is_clock_state_blocklisted(group, clk_names[i])) {
			clk_log_step_skip(TAG_CLK_STATE_TEST, group->name,
					  clk_names[i], CLK_STATE_TEST_ACT_ENABLE_1);
			res.skip_clk_num++;
			continue;
		}

		/* Record initial prepare/enable state before testing */
		if (!clk_get_prepare_enable(clk_names[i], &initial_enable)) {
			clk_log_step_fail(TAG_CLK_STATE_TEST, group->name, clk_names[i],
					  CLK_TEST_ACT_READ_INITIAL,
					  VAL_UNKNOWN, VAL_UNKNOWN,
					  CLK_TEST_REASON_READ_FAIL, errno);
			is_clock_ok = 0;
		}

		/* Step 1: ENABLE_1 (Only if initial state read succeeded) */
		if (is_clock_ok &&
		    !verify_clock_state(group->name, clk_names[i], 1,
					CLK_STATE_TEST_ACT_ENABLE_1, i, clk_count)) {
			is_clock_ok = 0;
		}

		/* Step 2: DISABLE_2 (Only if ENABLE_1 succeeded) */
		if (is_clock_ok &&
		    !verify_clock_state(group->name, clk_names[i], 0,
					CLK_STATE_TEST_ACT_DISABLE_2, i, clk_count)) {
			is_clock_ok = 0;
		}

		/* Step 3: ENABLE_3 (Only if DISABLE_2 succeeded) */
		if (is_clock_ok && !verify_clock_state(group->name, clk_names[i], 1,
						       CLK_STATE_TEST_ACT_ENABLE_3, i, clk_count)) {
			is_clock_ok = 0;
		}

		/* Step 4: Restore initial enable state (Only if prior steps succeeded) */
		if (is_clock_ok) {
			if (!verify_clock_state(group->name, clk_names[i], initial_enable,
						CLK_TEST_ACT_RESTORE_ENABLE, i, clk_count)) {
				is_clock_ok = 0;
			}
		}

		if (is_clock_ok)
			res.success_clk_num++;
		else
			res.fail_clk_num++;
	}

	if (!bste_kmsg_check_errors(kmsg_fd, "cpm_clk"))
		res.kmsg_pass = 0;

	res.is_pass = (res.fail_clk_num == 0 && res.kmsg_pass) ? 1 : 0;

	clk_log_suite_end(TAG_CLK_STATE_TEST, &res);
	return res.is_pass;
}

static int verify_set_clock_rate(const char *domain_name, const char *cname,
				 uint64_t target_rate, const char *action_str)
{
	uint64_t actual_rate = 0;

	if (!domain_name || !cname || !action_str) {
		BSTE_LOG_ERR("Invalid null parameter: domain_name, cname or action_str is NULL");
		return CLK_RES_FAIL;
	}

	if (!clk_set_rate(cname, target_rate)) {
		clk_log_step_fail(TAG_CLK_RATE_TEST, domain_name, cname,
				  action_str,
				  (int64_t)target_rate, VAL_UNKNOWN,
				  CLK_TEST_REASON_WRITE_FAIL, errno);
		return CLK_RES_FAIL;
	}

	if (!clk_get_rate(cname, &actual_rate)) {
		clk_log_step_fail(TAG_CLK_RATE_TEST, domain_name, cname,
				  action_str,
				  (int64_t)target_rate, VAL_UNKNOWN,
				  CLK_TEST_REASON_READ_FAIL, errno);
		return CLK_RES_FAIL;
	}

	if (actual_rate != target_rate) {
		clk_log_step_fail(TAG_CLK_RATE_TEST, domain_name, cname,
				  action_str,
				  (int64_t)target_rate, (int64_t)actual_rate,
				  CLK_TEST_REASON_VERIFY_FAIL, 0);
		return CLK_RES_FAIL;
	}

	return CLK_RES_PASS;
}

static int verify_clock_rates(const struct clk_group_map *group, const char *cname)
{
	uint64_t dt_rates[MAX_CLK_RATES];
	uint64_t initial_rate = 0;
	int dt_count;
	int clk_rates_pass = 1;
	int i;

	if (!group || !cname) {
		BSTE_LOG_ERR("Invalid null parameter: group or cname is NULL");
		return CLK_RES_FAIL;
	}

	dt_count = clk_get_dt_rates(cname, dt_rates, MAX_CLK_RATES);
	if (dt_count < 0) {
		clk_log_step_fail(TAG_CLK_RATE_TEST, group->name, cname,
				  CLK_RATE_TEST_ACT_GET_DT_RATES,
				  VAL_UNKNOWN, VAL_UNKNOWN, CLK_TEST_REASON_READ_FAIL, errno);
		return CLK_RES_FAIL;
	}

	if (dt_count == 0) {
		clk_log_step_skip(TAG_CLK_RATE_TEST, group->name, cname,
				  CLK_RATE_TEST_ACT_SET_RATE);
		return CLK_RES_SKIP;
	}

	if (!clk_set_prepare_enable(cname, 1)) {
		clk_log_step_fail(TAG_CLK_RATE_TEST, group->name, cname,
				  CLK_RATE_TEST_ACT_PREPARE_ENABLE,
				  1, VAL_UNKNOWN, CLK_TEST_REASON_WRITE_FAIL, errno);
		return CLK_RES_FAIL;
	}

	if (!clk_get_rate(cname, &initial_rate)) {
		clk_log_step_fail(TAG_CLK_RATE_TEST, group->name, cname,
				  CLK_RATE_TEST_ACT_GET_INIT_RATE,
				  VAL_UNKNOWN, VAL_UNKNOWN, CLK_TEST_REASON_READ_FAIL, errno);
		return CLK_RES_FAIL;
	}

	for (i = 0; i < dt_count; i++) {
		if (verify_set_clock_rate(group->name, cname, dt_rates[i],
					  CLK_RATE_TEST_ACT_SET_RATE) != CLK_RES_PASS)
			clk_rates_pass = 0;
	}

	/* Restore initial clock rate */
	if (verify_set_clock_rate(group->name, cname, initial_rate,
				  CLK_RATE_TEST_ACT_RESTORE_RATE) != CLK_RES_PASS)
		clk_rates_pass = 0;

	if (clk_rates_pass)
		clk_log_step_pass(TAG_CLK_RATE_TEST, group->name, cname,
				  CLK_RATE_TEST_ACT_SET_RATE);

	return clk_rates_pass ? CLK_RES_PASS : CLK_RES_FAIL;
}

static int run_verify_rate_test(const struct clk_group_map *group,
				char clk_names[][MAX_CLK_NAME],
				int clk_count, int kmsg_fd)
{
	struct clk_test_result res = { 0 };
	int i;
	int initial_enable = 0;
	int ret;

	res.suite = group->name;
	res.kmsg_pass = 1;

	clk_log_suite_start(TAG_CLK_RATE_TEST, group->name, clk_count);

	BSTE_LOG_INFO("Verifying rates for %d clocks in domain '%s'", clk_count, group->name);

	for (i = 0; i < clk_count; i++) {
		if (clk_is_clock_rate_blocklisted(group, clk_names[i])) {
			clk_log_step_skip(TAG_CLK_RATE_TEST, group->name,
					  clk_names[i], CLK_RATE_TEST_ACT_SET_RATE);
			res.skip_clk_num++;
			continue;
		}

		/* Record initial prepare/enable state before testing */
		if (!clk_get_prepare_enable(clk_names[i], &initial_enable)) {
			clk_log_step_fail(TAG_CLK_RATE_TEST, group->name, clk_names[i],
					  CLK_TEST_ACT_READ_INITIAL,
					  VAL_UNKNOWN, VAL_UNKNOWN,
					  CLK_TEST_REASON_READ_FAIL, errno);
			res.fail_clk_num++;
			continue;
		}

		ret = verify_clock_rates(group, clk_names[i]);

		/* Restore initial clock enable state */
		if (!clk_set_prepare_enable(clk_names[i], initial_enable)) {
			clk_log_step_fail(TAG_CLK_RATE_TEST, group->name, clk_names[i],
					  CLK_TEST_ACT_RESTORE_ENABLE,
					  initial_enable, VAL_UNKNOWN,
					  CLK_TEST_REASON_WRITE_FAIL, errno);
			ret = CLK_RES_FAIL;
		}

		if (ret == CLK_RES_PASS)
			res.success_clk_num++;
		else if (ret == CLK_RES_SKIP)
			res.skip_clk_num++;
		else
			res.fail_clk_num++;
	}

	if (!bste_kmsg_check_errors(kmsg_fd, "cpm_clk"))
		res.kmsg_pass = 0;

	res.is_pass = (res.fail_clk_num == 0 && res.kmsg_pass) ? 1 : 0;

	clk_log_suite_end(TAG_CLK_RATE_TEST, &res);
	return res.is_pass;
}

int main(int argc, char *argv[])
{
	char clk_names[MAX_GROUP_CLOCKS][MAX_CLK_NAME];
	const char *subcmd;
	const char *grp_name;
	const struct clk_group_map *group = NULL;
	clk_test_fn test_fn = NULL;
	int kmsg_fd;
	int clk_count;
	int ret;

	if (argc < 3) {
		BSTE_LOG_ERR("Usage: %s <state|rate> <group>", argv[0]);
		return 1;
	}

	subcmd = argv[1];
	grp_name = argv[2];

	if (!strcmp(subcmd, "state")) {
		test_fn = run_verify_state_test;
	} else if (!strcmp(subcmd, "rate")) {
		test_fn = run_verify_rate_test;
	} else {
		BSTE_LOG_ERR("Unsupported subcommand '%s': only 'state' and 'rate' are supported",
			     subcmd);
		return 1;
	}

	group = clk_find_group(grp_name);
	if (!group) {
		BSTE_LOG_ERR("Unknown or unsupported clock group: '%s'", grp_name);
		return 1;
	}

	if (!clk_control_init())
		return 1;

	clk_count = clk_scan_group_clocks(group->lpcm_id, clk_names, MAX_GROUP_CLOCKS);
	if (clk_count < 0) {
		BSTE_LOG_ERR("Failed to scan clocks for group '%s'", group->name);
		return 1;
	}

	if (clk_count == 0) {
		BSTE_LOG_ERR("No clocks found for group '%s'", group->name);
		return 1;
	}

	kmsg_fd = bste_kmsg_monitor_open();
	if (kmsg_fd < 0)
		return 1;

	ret = test_fn(group, clk_names, clk_count, kmsg_fd);

	bste_kmsg_monitor_close(&kmsg_fd);
	return ret ? 0 : 1;
}
