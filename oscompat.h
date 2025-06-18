#ifndef __OSCOMPAT_H__
#define __OSCOMPAT_H__

#ifndef _WIN32

#include <err.h>

#define O_BINARY 0

#else // _WIN32

#include <sys/time.h>
#include <stdbool.h>

void timeradd(const struct timeval *a, const struct timeval *b, struct timeval *result);

void err(int eval, const char *fmt, ...);
void errx(int eval, const char *fmt, ...);
void warn(const char *fmt, ...);
void warnx(const char *fmt, ...);

#endif

#endif
