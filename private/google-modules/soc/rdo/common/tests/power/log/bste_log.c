// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include "bste_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#define BSTE_LOG_FD 3
#define BSTE_BUF_SIZE 8192

static char bste_level_char(enum bste_log_level level)
{
	switch (level) {
	case BSTE_LVL_DEBUG:
		return 'D';
	case BSTE_LVL_INFO:
		return 'I';
	case BSTE_LVL_WARN:
		return 'W';
	case BSTE_LVL_ERROR:
		return 'E';
	default:
		return 'D';
	}
}

static void bste_write_log_fd(int fd, const char *buf, size_t len)
{
	size_t written = 0;
	ssize_t n;

	if (fd < 0 || fcntl(fd, F_GETFD) < 0)
		return;

	while (written < len) {
		n = write(fd, buf + written, len - written);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			break;
		}
		written += (size_t)n;
	}
}

void bste_log_fmt(enum bste_log_level level, const char *fmt, ...)
{
	char buf[BSTE_BUF_SIZE];
	va_list args;
	size_t offset;
	int n;

	n = snprintf(buf, BSTE_BUF_SIZE, "%c: ", bste_level_char(level));
	if (n < 0 || (size_t)n >= BSTE_BUF_SIZE) {
		fputs("E: Failed to format log level prefix\n", stderr);
		fflush(stderr);
		return;
	}
	offset = (size_t)n;

	if (fmt) {
		va_start(args, fmt);
		n = vsnprintf(buf + offset, BSTE_BUF_SIZE - offset, fmt, args);
		va_end(args);
		if (n > 0)
			offset += (size_t)n;
	}

	if (offset >= BSTE_BUF_SIZE - 2)
		offset = BSTE_BUF_SIZE - 2;

	buf[offset++] = '\n';
	buf[offset] = '\0';

	if (level >= BSTE_LVL_WARN) {
		fputs(buf, stderr);
		fflush(stderr);
		bste_write_log_fd(BSTE_LOG_FD, buf, offset);
	} else {
		fputs(buf, stdout);
		fflush(stdout);
	}
}
