/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2026 Google LLC */

#ifndef BSTE_LOG_H
#define BSTE_LOG_H

#include <stdio.h>

enum bste_log_level {
	BSTE_LVL_DEBUG = 0,
	BSTE_LVL_INFO  = 1,
	BSTE_LVL_WARN  = 2,
	BSTE_LVL_ERROR = 3,
};

#ifndef BSTE_PRINTF
#define BSTE_PRINTF(a, b) __attribute__((__format__(__printf__, a, b)))
#endif

void bste_log_fmt(enum bste_log_level level, const char *fmt, ...) BSTE_PRINTF(2, 3);

#define BSTE_LOG_DEBUG(fmt, ...) bste_log_fmt(BSTE_LVL_DEBUG, fmt, ##__VA_ARGS__)
#define BSTE_LOG_INFO(fmt, ...)  bste_log_fmt(BSTE_LVL_INFO, fmt, ##__VA_ARGS__)
#define BSTE_LOG_WARN(fmt, ...)  bste_log_fmt(BSTE_LVL_WARN, fmt, ##__VA_ARGS__)
#define BSTE_LOG_ERR(fmt, ...)   bste_log_fmt(BSTE_LVL_ERROR, fmt, ##__VA_ARGS__)

#endif /* BSTE_LOG_H */
