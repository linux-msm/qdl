/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __OSCOMPAT_H__
#define __OSCOMPAT_H__

#include <ctype.h>
#include <stdbool.h>

#ifndef _WIN32

#include <err.h>

#define O_BINARY 0

#else // _WIN32

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

#endif
