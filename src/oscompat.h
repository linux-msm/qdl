/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __OSCOMPAT_H__
#define __OSCOMPAT_H__

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#ifndef _WIN32

#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define O_BINARY 0

#else // _WIN32

#include <direct.h>
#include <sys/time.h>

void timeradd(const struct timeval *a, const struct timeval *b, struct timeval *result);

#endif

/**
 * path_is_absolute() - check if a path is absolute
 * @path: path string to check
 *
 * On POSIX systems, a path starting with '/' is absolute.
 * On Windows, absolute paths are either drive-letter paths (e.g. "C:\...")
 * or UNC paths (e.g. "\\server\share").
 *
 * Returns: true if @path is absolute, false otherwise
 */
static inline bool path_is_absolute(const char *path)
{
#ifndef _WIN32
	return path[0] == '/';
#else
	if (path[0] == '\0')
		return false;
	return (isalpha((unsigned char)path[0]) && path[1] == ':') ||
	       (path[0] == '\\' && path[1] == '\\');
#endif
}

/**
 * qdl_open_device_node() - open a device node for binary protocol traffic
 * @path: device node to open, e.g. "/dev/mhi0_QAIC_SAHARA" or "/dev/ttyUSB0"
 *
 * Opens @path read-write in binary mode, so that the Windows CRT does not
 * rewrite 0x0a on the way out or end a read at the first 0x1a.
 *
 * Serial ports arrive in canonical mode, where the line discipline echoes
 * input and rewrites the stream; a protocol carrying binary payloads cannot
 * survive that, so a node that turns out to be a terminal is switched to raw
 * mode. Nodes that are not terminals - character devices such as the MHI
 * Sahara endpoints - pass data through untouched and need no such setup.
 *
 * Returns: an open file descriptor, or -1 with errno set on failure.
 */
static inline int qdl_open_device_node(const char *path)
{
	int fd;
#ifndef _WIN32
	struct termios tio;
	int saved_errno;

	fd = open(path, O_RDWR | O_BINARY);
	if (fd < 0)
		return -1;

	if (!isatty(fd))
		return fd;

	if (tcgetattr(fd, &tio) < 0)
		goto err_close;

	cfmakeraw(&tio);

	if (tcsetattr(fd, TCSANOW, &tio) < 0)
		goto err_close;

	return fd;

err_close:
	saved_errno = errno;
	close(fd);
	errno = saved_errno;
	return -1;
#else
	fd = open(path, O_RDWR | O_BINARY);
	if (fd < 0)
		return -1;

	return fd;
#endif
}

/**
 * qdl_mkdir_p() - create @path and any missing parent directories
 * @path: directory path to create
 *
 * Mirrors "mkdir -p": an already existing directory is not an error.
 * Both '/' and (on Windows) '\\' are treated as path separators.
 *
 * Returns: 0 on success, -1 with errno set on failure.
 */
static inline int qdl_mkdir_p(const char *path)
{
	char tmp[PATH_MAX];
	size_t len;
	size_t i;

	len = strlen(path);
	if (len == 0)
		return 0;
	if (len >= sizeof(tmp)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(tmp, path, len + 1);

	/* Create each prefix in turn, then the full path at the terminator. */
	for (i = 1; i <= len; i++) {
		char c = tmp[i];
		char prev = tmp[i - 1];
		bool sep = c == '\0' || c == '/';
		bool prev_sep = prev == '/';
		char saved;
#ifdef _WIN32
		sep = sep || c == '\\';
		prev_sep = prev_sep || prev == '\\';
		/* Don't try to create a bare drive prefix such as "C:". */
		if (sep && prev == ':')
			continue;
#endif
		/* Act only at separators, and skip empty components. */
		if (!sep || prev_sep)
			continue;

		saved = tmp[i];
		tmp[i] = '\0';
#ifdef _WIN32
		if (_mkdir(tmp) != 0 && errno != EEXIST)
#else
		if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
#endif
			return -1;
		tmp[i] = saved;
	}

	return 0;
}

/**
 * qdl_process_result - result of running a child process
 * @stdout_buf: malloc'd, NUL-terminated stdout; caller must free
 * @stdout_len: length of stdout_buf (excluding NUL terminator)
 * @exit_code: exit code of the child process (valid only if !timed_out)
 * @timed_out: true if the process was killed due to timeout
 */
struct qdl_process_result {
	char *stdout_buf;
	size_t stdout_len;
	int exit_code;
	bool timed_out;
};

/**
 * qdl_run_capture() - run a child process and capture its stdout
 * @argv: NULL-terminated argv vector; argv[0] is the program name
 * @timeout_ms: timeout in milliseconds; 0 means no timeout
 * @result: output structure to be filled
 *
 * Runs a child process without invoking a shell. argv[0] is searched in PATH.
 * Captures stdout into a malloc'd buffer. On timeout, the process is killed
 * and result->timed_out is set to true.
 *
 * Returns: 0 on success, -1 on error (with errno set).
 * The caller must free result->stdout_buf.
 */
int qdl_run_capture(char *const argv[], int timeout_ms, struct qdl_process_result *result);

/**
 * qdl_process_result_free() - free resources from qdl_run_capture()
 * @result: result structure to free
 */
void qdl_process_result_free(struct qdl_process_result *result);

#endif
