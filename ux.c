#include <stdarg.h>

#include "qdl.h"

/*
 * Levels of output:
 *
 * error: used to signal errors to the user
 * info: used to inform the user about progress
 * logs: log prints from the device
 * debug: protocol logs
 */

void ux_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fflush(stderr);
}

void ux_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

void ux_log(const char *fmt, ...)
{
	va_list ap;

	if (!qdl_debug)
		return;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

void ux_debug(const char *fmt, ...)
{
	va_list ap;

	if (!qdl_debug)
		return;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}
