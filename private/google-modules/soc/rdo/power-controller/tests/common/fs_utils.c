// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC */

#include "fs_utils.h"

#include <ctype.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static size_t trim_str(char *str, size_t len)
{
	char *start;
	size_t new_len;

	if (!str || len == 0)
		return 0;

	while (len > 0 && (str[len - 1] == '\0' || isspace((unsigned char)str[len - 1])))
		str[--len] = '\0';

	start = str;
	while (*start != '\0' && isspace((unsigned char)*start))
		start++;

	if (start != str) {
		new_len = len - (size_t)(start - str);
		memmove(str, start, new_len + 1);
		return new_len;
	}

	return len;
}

int fs_file_exists(const char *path)
{
	struct stat st;

	if (!path)
		return 0;

	return stat(path, &st) == 0;
}

int fs_is_dir(const char *path)
{
	struct stat st;

	if (!path)
		return 0;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/*
 * is_valid_text - Verify buffer contains only printable text and whitespace.
 * Rejects embedded null characters ('\0') and non-whitespace control bytes.
 */
static int is_valid_text(const unsigned char *buf, size_t len)
{
	size_t i;

	if (!buf)
		return 0;

	for (i = 0; i < len; i++) {
		unsigned char c = buf[i];

		if (c == '\0')
			return 0;
		if (!isprint(c) && !isspace(c))
			return 0;
	}
	return 1;
}

/*
 * fs_read_bytes - Read up to out_size bytes from a file with EINTR retry loop.
 * Probes 1 extra byte if out_size bytes are read to detect buffer overflow.
 */
static ssize_t fs_read_bytes(const char *path, char *buf, size_t out_size)
{
	size_t total = 0;
	int fd;
	ssize_t n;

	if (!path || !buf || out_size == 0)
		return -1;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "ERROR: Failed to open %s for reading: %s\n",
			path, strerror(errno));
		return -1;
	}

	while (total < out_size) {
		n = read(fd, buf + total, out_size - total);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "ERROR: Failed reading from %s: %s\n",
				path, strerror(errno));
			close(fd);
			return -1;
		}
		if (n == 0)
			break;
		total += n;
	}

	if (total == out_size) {
		char probe_byte;
		ssize_t extra = read(fd, &probe_byte, 1);

		if (extra > 0) {
			fprintf(stderr,
				"ERROR: File %s exceeds buffer capacity (%zu bytes)\n",
				path, out_size);
			close(fd);
			return -1;
		}
	}
	close(fd);
	return (ssize_t)total;
}

ssize_t fs_read_text_file(const char *path, char *out, size_t out_size)
{
	size_t len;
	ssize_t n;

	if (!path || !out || out_size == 0)
		return -1;

	out[0] = '\0';
	n = fs_read_bytes(path, out, out_size - 1);
	if (n < 0) {
		out[0] = '\0';
		return -1;
	}

	out[n] = '\0';
	len = trim_str(out, n);

	if (!is_valid_text((unsigned char *)out, len)) {
		fprintf(stderr, "ERROR: File %s contains non-text binary data\n", path);
		out[0] = '\0';
		return -1;
	}

	return (ssize_t)len;
}

char *fs_read_text_line(FILE *fp, char *buf, size_t buf_size)
{
	size_t len;

	if (!fp || !buf || buf_size == 0)
		return NULL;

	if (!fgets(buf, buf_size, fp))
		return NULL;

	len = strlen(buf);
	len = trim_str(buf, len);

	if (!is_valid_text((const unsigned char *)buf, len)) {
		fprintf(stderr, "ERROR: Stream contains non-text binary data\n");
		buf[0] = '\0';
		return NULL;
	}

	return buf;
}

int fs_write_text_file(const char *path, const char *data)
{
	const char *buf;
	size_t len;
	int fd;
	ssize_t n;

	if (!path || !data)
		return 0;

	len = strlen(data);
	if (!is_valid_text((const unsigned char *)data, len)) {
		fprintf(stderr, "ERROR: Attempted to write non-text binary data to %s\n", path);
		return 0;
	}

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "ERROR: Failed to open %s for writing: %s\n",
			path, strerror(errno));
		return 0;
	}

	buf = data;
	while (len > 0) {
		n = write(fd, buf, len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "ERROR: Failed writing '%s' to %s: %s\n",
				data, path, strerror(errno));
			close(fd);
			return 0;
		}
		if (n == 0) {
			fprintf(stderr, "ERROR: Zero bytes written to %s\n", path);
			close(fd);
			return 0;
		}
		len -= n;
		buf += n;
	}
	close(fd);
	return 1;
}

int fs_read_u32_be(const char *path, uint32_t *out_val)
{
	uint32_t be_val;
	ssize_t n;

	if (!path || !out_val)
		return 0;

	n = fs_read_bytes(path, (char *)&be_val, sizeof(be_val));
	if (n != sizeof(be_val)) {
		if (n >= 0)
			fprintf(stderr, "ERROR: Short read (%zd bytes) from %s\n",
				n, path);
		return 0;
	}

	*out_val = be32toh(be_val);
	return 1;
}
