/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __OSCOMPAT_H__
#define __OSCOMPAT_H__

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#ifndef _WIN32

#include <err.h>
#include <sys/stat.h>

#define O_BINARY 0

#else // _WIN32

#include <direct.h>
#include <sys/time.h>

void timeradd(const struct timeval *a, const struct timeval *b, struct timeval *result);

void err(int eval, const char *fmt, ...);
void errx(int eval, const char *fmt, ...);
void warn(const char *fmt, ...);
void warnx(const char *fmt, ...);

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
	return (isalpha(path[0]) && path[1] == ':') ||
	       (path[0] == '\\' && path[1] == '\\');
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

#endif
