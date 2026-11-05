// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include "kmsg_utils.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "bste_log.h"

#define BSTE_KMSG_MAX_READS 10000

int bste_kmsg_monitor_open(void)
{
	int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);

	if (fd < 0) {
		BSTE_LOG_ERR("Failed to open /dev/kmsg: %s (errno=%d)", strerror(errno), errno);
		return -1;
	}

	if (lseek(fd, 0, SEEK_END) == (off_t)-1) {
		BSTE_LOG_ERR("Failed to seek to end of /dev/kmsg: %s (errno=%d)",
			     strerror(errno), errno);
		close(fd);
		return -1;
	}

	return fd;
}

static int kmsg_has_error_tag(const char *msg, const char *log_tag)
{
	unsigned long prio;
	int log_level;
	char *endp;
	/* 64 bytes is sufficient for all current subsystem tags (e.g., "cpm_rst", "cpm_clk") */
	char alt_tag[64];
	int has_alt_tag = 0;
	size_t len;
	size_t i;

	if (!msg || !log_tag)
		return 0;

	len = strlen(log_tag);
	if (len < sizeof(alt_tag)) {
		for (i = 0; i <= len; i++) {
			if (log_tag[i] == '_' || log_tag[i] == '-') {
				alt_tag[i] = (log_tag[i] == '_') ? '-' : '_';
				has_alt_tag = 1;
			} else {
				alt_tag[i] = log_tag[i];
			}
		}
	} else {
		alt_tag[0] = '\0';
	}

	if (!strstr(msg, log_tag) && (!has_alt_tag || !strstr(msg, alt_tag)))
		return 0;

	prio = strtoul(msg, &endp, 10);
	if (endp == msg || *endp != ',')
		return 0;

	log_level = (int)(prio & LOG_PRIMASK);
	if (log_level <= LOG_WARNING)
		return 1;

	return 0;
}

int bste_kmsg_check_errors(int kmsg_fd, const char *log_tag)
{
	char buf[8192];
	ssize_t n;
	int err_found = 0;
	int num_kmsg_reads = 0;

	if (kmsg_fd < 0 || !log_tag) {
		if (kmsg_fd < 0)
			BSTE_LOG_ERR("Invalid file descriptor passed to kmsg verification");
		if (!log_tag)
			BSTE_LOG_ERR("NULL log_tag passed to kmsg verification");
		return 0;
	}

	while (num_kmsg_reads < BSTE_KMSG_MAX_READS &&
	       ((n = read(kmsg_fd, buf, sizeof(buf) - 1)) > 0 ||
		(n < 0 && (errno == EPIPE || errno == EINTR)))) {
		num_kmsg_reads++;
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && errno == EPIPE) {
			BSTE_LOG_ERR("Kernel log ring buffer overflowed during verification");
			err_found = 1;
			continue;
		}
		buf[n] = '\0';
		/* Strip trailing whitespace to prevent double newlines in log output */
		while (n > 0 && isspace((unsigned char)buf[n - 1]))
			buf[--n] = '\0';
		if (kmsg_has_error_tag(buf, log_tag)) {
			BSTE_LOG_ERR("Kernel log error detected: %s", buf);
			err_found = 1;
		}
	}
	if (num_kmsg_reads >= BSTE_KMSG_MAX_READS)
		BSTE_LOG_WARN("Reached max read limit (%d) scanning /dev/kmsg",
			      BSTE_KMSG_MAX_READS);
	if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
		BSTE_LOG_ERR("Failed reading from /dev/kmsg: %s (errno=%d)",
			     strerror(errno), errno);
		err_found = 1;
	}
	return !err_found;
}

void bste_kmsg_monitor_close(int *kmsg_fd)
{
	if (kmsg_fd && *kmsg_fd >= 0) {
		close(*kmsg_fd);
		*kmsg_fd = -1;
	}
}
