#ifndef __OSCOMPAT_H__
#define __OSCOMPAT_H__

#ifndef _WIN32

#include <err.h>

#else // _WIN32

#include <sys/time.h>
#include <stdbool.h>

// TODO: improve err functions

#define err(code, ...) do { ux_err(__VA_ARGS__); exit(code); } while(false)
#define errx(code, ...) do { ux_err(__VA_ARGS__); exit(code); } while(false)
#define warn(...) ux_info(__VA_ARGS__)
#define warnx(...) ux_info(__VA_ARGS__)


void timeradd(const struct timeval *a, const struct timeval *b, struct timeval *result);

#endif

#endif