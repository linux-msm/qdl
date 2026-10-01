// SPDX-License-Identifier: BSD-3-Clause
#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oscompat.h"

void timeradd(const struct timeval *a, const struct timeval *b, struct timeval *result)
{
	result->tv_sec = a->tv_sec + b->tv_sec;
	result->tv_usec = a->tv_usec + b->tv_usec;
	if (result->tv_usec >= 1000000) {
		result->tv_sec += 1;
		result->tv_usec -= 1000000;
	}
}

#endif // _WIN32
