#include "oscompat.h"

#ifdef _WIN32

const char *__progname = "qdl";

void timeradd(const struct timeval *a, const struct timeval *b, struct timeval *result) {
    result->tv_sec = a->tv_sec + b->tv_sec;
    result->tv_usec = a->tv_usec + b->tv_usec;
    if (result->tv_usec >= 1000000) {
        result->tv_sec += 1;
        result->tv_usec -= 1000000;
    }
}

#endif // _WIN32