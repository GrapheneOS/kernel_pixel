// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include "rst_control.h"
#include "rst_defs.h"
#include "../power-domain/pd_control.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <unistd.h>

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
		fprintf(stderr, "ERROR: %s called with NULL domain pointer\n",
			__func__);
		return 0;
	}

	printf("  [test setup] Powering ON %s\n", dom->name);
	if (!pd_set_debugfs_state(dom->name, PD_STATE_ON)) {
		fprintf(stderr, "ERROR: Failed to power ON %s\n", dom->name);
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
		fprintf(stderr, "ERROR: %s called with NULL domain name\n",
			__func__);
		return 0;
	}

	printf("=== Ensuring prerequisite power domain subtree ON: %s ===\n", req_pd);
	if (!pd_control_init(req_pd)) {
		fprintf(stderr, "ERROR: Failed to init power domain: %s\n", req_pd);
		return 0;
	}

	root = pd_find_domain(req_pd);
	if (!root) {
		fprintf(stderr, "ERROR: Could not find domain node: %s\n", req_pd);
		return 0;
	}

	return power_on_domain_recursive(root);
}

static int open_kmsg_monitor(void)
{
	int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);

	if (fd >= 0)
		lseek(fd, 0, SEEK_END);

	return fd;
}

static int kmsg_has_cpm_rst_error(const char *msg)
{
	unsigned long prio;
	int log_level;
	char *endp;

	if (!msg || (!strstr(msg, "cpm_rst") && !strstr(msg, "cpm-rst")))
		return 0;

	prio = strtoul(msg, &endp, 10);
	if (endp == msg || *endp != ',')
		return 0;

	log_level = (int)(prio & LOG_PRIMASK);
	if (log_level <= LOG_WARNING)
		return 1;

	return 0;
}

static int check_kmsg_errors(int kmsg_fd)
{
	char buf[8192];
	ssize_t n;
	int err_found = 0;

	if (kmsg_fd < 0)
		return 1;

	while ((n = read(kmsg_fd, buf, sizeof(buf) - 1)) > 0 ||
	       (n < 0 && (errno == EPIPE || errno == EINTR))) {
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && errno == EPIPE) {
			fprintf(stderr,
				"ERROR: Kernel log ring buffer overflowed during test verification (EPIPE)\n");
			err_found = 1;
			continue;
		}
		buf[n] = '\0';
		if (kmsg_has_cpm_rst_error(buf)) {
			fprintf(stderr, "ERROR: Kernel log reported cpm_rst failure: %s\n",
				buf);
			err_found = 1;
		}
	}
	if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
		fprintf(stderr, "ERROR: Failed reading from /dev/kmsg: %s\n",
			strerror(errno));
		err_found = 1;
	}
	close(kmsg_fd);
	return !err_found;
}

int main(int argc, char *argv[])
{
	const struct rst_domain_info *info;
	const char *domain_name;
	int reset_num;
	int kmsg_fd;
	int i;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <domain_name>\n", argv[0]);
		return 1;
	}

	domain_name = argv[1];
	if (!rst_control_init())
		return 1;

	info = rst_find_domain(domain_name);
	if (!info) {
		fprintf(stderr, "ERROR: Unknown reset domain: %s\n", domain_name);
		return 1;
	}

	if (info->required_pd) {
		if (!ensure_required_power_domain(info->required_pd)) {
			fprintf(stderr,
				"ERROR: Failed to power ON prerequisite domain subtree (%s) for %s\n",
				info->required_pd, info->name);
			return 1;
		}
	}

	reset_num = rst_get_reset_num(info->name);
	if (reset_num <= 0) {
		fprintf(stderr, "ERROR: Invalid reset count (%d) for domain: %s\n",
			reset_num, info->name);
		return 1;
	}

	kmsg_fd = open_kmsg_monitor();
	if (kmsg_fd < 0) {
		fprintf(stderr, "ERROR: Failed to open /dev/kmsg for log verification: %s\n",
			strerror(errno));
		return 1;
	}

	printf("=== Verifying %d reset lines for %s ===\n", reset_num, info->name);
	for (i = 0; i < reset_num; i++) {
		printf("  [test case] Asserting %s ID %d\n", info->name, i);
		if (!rst_assert(info->name, i)) {
			fprintf(stderr, "ERROR: Failed asserting %s ID %d\n", info->name, i);
			check_kmsg_errors(kmsg_fd);
			return 1;
		}

		printf("  [test case] Deasserting %s ID %d\n", info->name, i);
		if (!rst_deassert(info->name, i)) {
			fprintf(stderr, "ERROR: Failed deasserting %s ID %d\n", info->name, i);
			check_kmsg_errors(kmsg_fd);
			return 1;
		}
	}

	if (!check_kmsg_errors(kmsg_fd)) {
		fprintf(stderr, "ERROR: Verification failed due to kernel log errors on %s\n",
			info->name);
		return 1;
	}

	printf("=== Successfully verified %d reset lines for %s ===\n", reset_num, info->name);
	return 0;
}
