/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2026 Google LLC */

#ifndef FS_UTILS_H
#define FS_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#define FS_PATH_MAX 512

/**
 * fs_file_exists - Check whether a filesystem path exists.
 * @path: Absolute or relative filesystem path.
 *
 * Return: 1 if the path exists, 0 otherwise.
 */
int fs_file_exists(const char *path);

/**
 * fs_is_dir - Check whether a filesystem path exists and is a directory.
 * @path: Absolute or relative filesystem path.
 *
 * Return: 1 if the path is a directory, 0 otherwise.
 */
int fs_is_dir(const char *path);

/**
 * fs_read_text_file - Read a text file into a buffer and trim whitespace.
 * @path: Filesystem path to read from.
 * @out: Destination string buffer.
 * @out_size: Buffer capacity in bytes (> 0).
 *
 * Verifies valid text content and trims leading/trailing whitespace.
 * Fails if the file exceeds @out_size - 1 bytes or contains non-text data.
 *
 * Return: Length of the trimmed string on success (>= 0), or -1 on failure.
 */
ssize_t fs_read_text_file(const char *path, char *out, size_t out_size);

/**
 * fs_read_text_line - Read a line of text from a stream and trim whitespace.
 * @fp: Open file stream.
 * @buf: Destination string buffer.
 * @buf_size: Buffer capacity in bytes (> 0).
 *
 * Return: Pointer to @buf on success, or NULL on error or non-text content.
 */
char *fs_read_text_line(FILE *fp, char *buf, size_t buf_size);

/**
 * fs_write_text_file - Write a null-terminated text string to a file.
 * @path: Filesystem path to write to.
 * @data: Null-terminated text string to write.
 *
 * Return: 1 on success, 0 on failure or if non-text data is detected.
 */
int fs_write_text_file(const char *path, const char *data);

/**
 * fs_read_u32_be - Read a 32-bit big-endian integer from a file.
 * @path: Filesystem path to read from.
 * @out_val: Pointer to store converted host-endian unsigned integer.
 *
 * Return: 1 on success, 0 on failure.
 */
int fs_read_u32_be(const char *path, uint32_t *out_val);

/**
 * fs_read_u64_be_array - Read an array of 64-bit big-endian integers from a file.
 * @path: Filesystem path to read from.
 * @array: Destination array of 64-bit unsigned integers.
 * @max_elements: Maximum number of elements @array can store.
 *
 * Converts each 64-bit big-endian integer to host-endian representation.
 *
 * Return: Number of 64-bit elements read on success (>= 0), or -1 on failure.
 */
ssize_t fs_read_u64_be_array(const char *path, uint64_t *array, size_t max_elements);

#endif /* FS_UTILS_H */
