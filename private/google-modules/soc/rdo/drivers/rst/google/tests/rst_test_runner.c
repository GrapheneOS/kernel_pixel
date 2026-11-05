// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <unistd.h>

#include "bste_log.h"
#include "kmsg_utils.h"
#include "pd_control.h"

#include "rst_control.h"
#include "rst_defs.h"

#define RST_TEST_SUITE_START_FMT "[RST_TEST] SUITE=%s ACTION=START COUNT=%d"
#define RST_TEST_STEP_PASS_FMT   "[RST_TEST] DOMAIN=%s RESET_ID=%d ACTION=%s STATUS=OK"
#define RST_TEST_STEP_FAIL_FMT   "[RST_TEST] DOMAIN=%s RESET_ID=%d ACTION=%s STATUS=FAIL REASON=%s ERRNO=%d"
#define RST_TEST_SUITE_END_FMT   "[RST_TEST] SUITE=%s ACTION=END STATUS=%s ASSERT_DEASSERT_PASS=%d FAILED_RESET_ID=%d KMSG_PASS=%d"

#define RST_TEST_ACT_ASSERT          "ASSERT"
#define RST_TEST_ACT_DEASSERT        "DEASSERT"
#define RST_TEST_REASON_WRITE_FAIL   "WRITE_FAIL"

struct rst_test_result {
	const char *suite;
	int is_pass;
	int assert_deassert_pass;
	int failed_reset_id;
	int kmsg_pass;
};

static void rst_log_suite_start(const char *suite, int count)
{
	BSTE_LOG_INFO(RST_TEST_SUITE_START_FMT, suite, count);
}

static void rst_log_step_pass(const char *domain, int reset_id, const char *action)
{
	BSTE_LOG_INFO(RST_TEST_STEP_PASS_FMT, domain, reset_id, action);
}

static void rst_log_step_fail(const char *domain, int reset_id, const char *action,
			      const char *reason, int err_code)
{
	BSTE_LOG_ERR(RST_TEST_STEP_FAIL_FMT, domain, reset_id, action, reason, err_code);
}

static void rst_log_suite_end(const struct rst_test_result *res)
{
	const char *status_str = res->is_pass ? "PASS" : "FAIL";

	if (res->is_pass)
		BSTE_LOG_INFO(RST_TEST_SUITE_END_FMT, res->suite, status_str,
			      res->assert_deassert_pass,
			      res->failed_reset_id, res->kmsg_pass);
	else
		BSTE_LOG_ERR(RST_TEST_SUITE_END_FMT, res->suite, status_str,
			     res->assert_deassert_pass,
			     res->failed_reset_id, res->kmsg_pass);
}

/*
 * power_on_domain_recursive - Recursively power ON a domain and its descendants.
 * @dom: Domain tree node.
 *
 * Return: 1 on success, 0 on failure.
 */
static int power_on_domain_recursive(struct power_domain *dom)
{
	int i;

	if (!dom) {
		BSTE_LOG_ERR("%s: domain pointer is NULL", __func__);
		return 0;
	}

	BSTE_LOG_INFO("Powering ON prerequisite domain '%s'", dom->name);
	if (!pd_set_debugfs_state(dom->name, PD_STATE_ON)) {
		BSTE_LOG_ERR("Failed to set state to ON for domain '%s'", dom->name);
		return 0;
	}

	for (i = 0; i < dom->child_count; i++) {
		if (!power_on_domain_recursive(dom->children[i]))
			return 0;
	}
	return 1;
}

/*
 * ensure_required_power_domain - Ensure prerequisite root domain subtree is ON.
 * @req_pd: Prerequisite root domain name.
 *
 * Return: 1 on success, 0 on failure.
 */
static int ensure_required_power_domain(const char *req_pd)
{
	struct power_domain *root;

	if (!req_pd) {
		BSTE_LOG_ERR("%s: domain name is NULL", __func__);
		return 0;
	}

	if (!pd_control_init(req_pd)) {
		BSTE_LOG_ERR("Failed to init prerequisite power domain subtree '%s'", req_pd);
		return 0;
	}

	root = pd_find_domain(req_pd);
	if (!root) {
		BSTE_LOG_ERR("Could not find prerequisite domain node '%s'", req_pd);
		return 0;
	}

	return power_on_domain_recursive(root);
}

int main(int argc, char *argv[])
{
	const struct rst_domain_info *info;
	const char *domain_name;
	int reset_num;
	int kmsg_fd;
	int i;
	struct rst_test_result res = { 0 };

	if (argc < 2) {
		BSTE_LOG_ERR("Usage: %s <domain_name>", argv[0]);
		return 1;
	}

	domain_name = argv[1];
	if (!rst_control_init())
		return 1;

	info = rst_find_domain(domain_name);
	if (!info) {
		BSTE_LOG_ERR("Unknown reset domain specified: '%s'", domain_name);
		return 1;
	}

	if (info->required_pd) {
		if (!ensure_required_power_domain(info->required_pd)) {
			BSTE_LOG_ERR("Failed to power ON prerequisite domain subtree '%s'",
				     info->required_pd);
			return 1;
		}
	}

	reset_num = rst_get_reset_num(info->name);
	if (reset_num <= 0) {
		BSTE_LOG_ERR("Invalid or zero reset count (%d) read for domain '%s'",
			     reset_num, info->name);
		return 1;
	}

	kmsg_fd = bste_kmsg_monitor_open();
	if (kmsg_fd < 0)
		return 1;

	res.suite = info->name;
	res.assert_deassert_pass = 1;
	res.kmsg_pass = 1;
	res.failed_reset_id = -1;

	rst_log_suite_start(info->name, reset_num);

	for (i = 0; i < reset_num; i++) {
		BSTE_LOG_INFO("Asserting reset ID %d for domain '%s'", i, info->name);
		if (!rst_assert(info->name, i)) {
			rst_log_step_fail(info->name, i, RST_TEST_ACT_ASSERT,
					  RST_TEST_REASON_WRITE_FAIL, errno);
			res.assert_deassert_pass = 0;
			res.failed_reset_id = i;
			break;
		}
		rst_log_step_pass(info->name, i, RST_TEST_ACT_ASSERT);

		BSTE_LOG_INFO("Deasserting reset ID %d for domain '%s'", i, info->name);
		if (!rst_deassert(info->name, i)) {
			rst_log_step_fail(info->name, i, RST_TEST_ACT_DEASSERT,
					  RST_TEST_REASON_WRITE_FAIL, errno);
			res.assert_deassert_pass = 0;
			res.failed_reset_id = i;
			break;
		}
		rst_log_step_pass(info->name, i, RST_TEST_ACT_DEASSERT);
	}

	res.kmsg_pass = bste_kmsg_check_errors(kmsg_fd, "cpm_rst");
	bste_kmsg_monitor_close(&kmsg_fd);

	res.is_pass = (res.assert_deassert_pass && res.kmsg_pass) ? 1 : 0;

	rst_log_suite_end(&res);
	return res.is_pass ? 0 : 1;
}
