/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2026 Google LLC */

#ifndef BSTE_KMSG_UTILS_H
#define BSTE_KMSG_UTILS_H

/**
 * bste_kmsg_monitor_open() - Open /dev/kmsg in non-blocking mode and seek to end.
 *
 * Return: Non-negative file descriptor on success, negative on failure.
 */
int bste_kmsg_monitor_open(void);

/**
 * bste_kmsg_check_errors() - Read new kernel log lines and check for errors matching tag.
 * @kmsg_fd: Open file descriptor to /dev/kmsg in non-blocking mode.
 * @log_tag: Substring or tag string to look for (e.g., "cpm_rst", "cpm_clk").
 *
 * Reads all newly available entries in @kmsg_fd without closing the descriptor.
 * Checks for log entries with priority <= LOG_WARNING that contain @log_tag (or
 * hyphens replaced, e.g., "cpm-rst" when @log_tag is "cpm_rst"). Logs matching
 * entries via BSTE_LOG_ERR and reports ring buffer overflows via BSTE_LOG_ERR.
 *
 * Return: 1 if zero matching kernel errors were found, 0 if any errors detected.
 */
int bste_kmsg_check_errors(int kmsg_fd, const char *log_tag);

/**
 * bste_kmsg_monitor_close() - Close an open /dev/kmsg file descriptor safely and set pointer to -1.
 * @kmsg_fd: Pointer to file descriptor variable to close and reset to -1.
 */
void bste_kmsg_monitor_close(int *kmsg_fd);

#endif /* BSTE_KMSG_UTILS_H */
