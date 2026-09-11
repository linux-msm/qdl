// SPDX-License-Identifier: BSD-3-Clause
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#endif
#include <sys/time.h>
#include <unistd.h>

#include <libxml/xmlerror.h>
#include <libxml/xmlversion.h>

#include "qdl.h"

/* libxml2 2.12 const-qualified the xmlError pointer in this callback. */
#if LIBXML_VERSION >= 21200
typedef const xmlError * ux_xml_error_ptr;
#else
typedef xmlErrorPtr ux_xml_error_ptr;
#endif

#define UX_PROGRESS_REFRESH_RATE	10
#define UX_PROGRESS_SIZE_MAX		80

#define HASHES "################################################################################"
#define DASHES "--------------------------------------------------------------------------------"

static const char * const progress_hashes = HASHES;
static const char * const progress_dashes = DASHES;

static unsigned int ux_width;
static unsigned int ux_cur_line_length;

/* Whether the next character written to each stream starts a new line */
static bool ux_stdout_bol = true;
static bool ux_stderr_bol = true;

/*
 * Levels of output:
 *
 * error: used to signal errors to the user
 * info: used to inform the user about progress
 * logs: log prints from the device
 * debug: protocol logs
 *
 * Without --debug, messages are written verbatim. With --debug every level
 * is written in a uniform format, where each message starts with a
 * timestamp:
 *
 *   14:03:21.123 flashed "boot" successfully
 *
 * Continuation lines of a multi-line message are indented to the column
 * where the first line's text starts, so the message body stays aligned.
 * A message that does not end in a newline is continued by the next call
 * on the same stream without a new prefix.
 */

/* Write the "<time> " prefix, return its width */
static int ux_print_prefix(FILE *fp)
{
	const char *stamp = "00:00:00";
	struct timeval tv;
	struct tm *tm;
	char buf[32];

	gettimeofday(&tv, NULL);
	tm = localtime(&tv.tv_sec);
	if (tm && strftime(buf, sizeof(buf), "%H:%M:%S", tm))
		stamp = buf;

	return fprintf(fp, "%s.%03ld ", stamp, (long)(tv.tv_usec / 1000));
}

/*
 * Format a message and write it to @fp. In debug mode each line of the
 * message is prefixed, the first with the timestamp header and the rest
 * with padding of the same width, unless the line continues a previous
 * message that did not end with a newline. Blank lines are left bare.
 */
static void ux_vprint(FILE *fp, bool *bol, const char *fmt, va_list ap)
{
	char stack_buf[512];
	char *buf = stack_buf;
	const char *line;
	const char *end;
	int width = 0;
	va_list aq;
	int len;

	if (!qdl_debug) {
		vfprintf(fp, fmt, ap);
		fflush(fp);
		return;
	}

	va_copy(aq, ap);
	len = vsnprintf(stack_buf, sizeof(stack_buf), fmt, aq);
	va_end(aq);
	if (len < 0)
		return;

	if ((size_t)len >= sizeof(stack_buf)) {
		buf = malloc(len + 1);
		if (!buf)
			return;
		vsnprintf(buf, len + 1, fmt, ap);
	}

	for (line = buf; *line; line = end) {
		end = strchr(line, '\n');
		end = end ? end + 1 : line + strlen(line);

		if (*bol && *line != '\n') {
			if (!width)
				width = ux_print_prefix(fp);
			else
				fprintf(fp, "%*s", width, "");
		}

		fwrite(line, 1, end - line, fp);
		*bol = end[-1] == '\n';
	}

	if (buf != stack_buf)
		free(buf);
	fflush(fp);
}

/* Clear ux_cur_line_length characters of the progress bar from the screen */
static void ux_clear_line(void)
{
	if (!ux_cur_line_length)
		return;

	printf("%*s\r", ux_cur_line_length, "");
	fflush(stdout);
	ux_cur_line_length = 0;
}

/*
 * libxml2 emits parser diagnostics directly to stderr by default. Route them
 * through ux_err() so the file:line context is preserved while keeping output
 * consistent with the rest of the tool.
 */
static void ux_xml_error_handler(void *ctx __unused, ux_xml_error_ptr error)
{
	const char *level;

	if (!error || error->level == XML_ERR_NONE)
		return;

	switch (error->level) {
	case XML_ERR_WARNING:
		level = "warning";
		break;
	case XML_ERR_ERROR:
		level = "error";
		break;
	case XML_ERR_FATAL:
	default:
		level = "fatal";
		break;
	}

	if (error->file)
		ux_err("libxml2 %s: %s:%d: %s",
		       level, error->file, error->line, error->message);
	else
		ux_err("libxml2 %s: %s", level, error->message);
}

#ifdef _WIN32

void ux_init(void)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	int columns;

	HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);

	if (GetConsoleScreenBufferInfo(stdoutHandle, &csbi)) {
		columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
		ux_width = MIN(columns, UX_PROGRESS_SIZE_MAX);
	}

	xmlSetStructuredErrorFunc(NULL, ux_xml_error_handler);
}

#else

void ux_init(void)
{
	struct winsize w;
	int ret;

	ret = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	if (!ret)
		ux_width = MIN(w.ws_col, UX_PROGRESS_SIZE_MAX);

	xmlSetStructuredErrorFunc(NULL, ux_xml_error_handler);
}

#endif

void ux_err(const char *fmt, ...)
{
	va_list ap;

	ux_clear_line();

	va_start(ap, fmt);
	ux_vprint(stderr, &ux_stderr_bol, fmt, ap);
	va_end(ap);
}

/* Keep the err(3)/warn(3) text and errno semantics in the shared logger. */
static void ux_vreport(int error, bool with_errno, const char *fmt, va_list ap)
{
	extern const char *__progname;

	ux_err("%s: ", __progname);
	if (fmt) {
		ux_vprint(stderr, &ux_stderr_bol, fmt, ap);
		if (with_errno)
			ux_err(": ");
	}
	if (with_errno)
		ux_err("%s", strerror(error));
	ux_err("\n");
	errno = error;
}

void ux_warn(const char *fmt, ...)
{
	int error = errno;
	va_list ap;

	va_start(ap, fmt);
	ux_vreport(error, true, fmt, ap);
	va_end(ap);
}

void ux_warnx(const char *fmt, ...)
{
	int error = errno;
	va_list ap;

	va_start(ap, fmt);
	ux_vreport(error, false, fmt, ap);
	va_end(ap);
}

void ux_die(int status, const char *fmt, ...)
{
	int error = errno;
	va_list ap;

	va_start(ap, fmt);
	ux_vreport(error, true, fmt, ap);
	va_end(ap);
	exit(status);
}

void ux_diex(int status, const char *fmt, ...)
{
	int error = errno;
	va_list ap;

	va_start(ap, fmt);
	ux_vreport(error, false, fmt, ap);
	va_end(ap);
	exit(status);
}

void ux_info(const char *fmt, ...)
{
	va_list ap;

	ux_clear_line();

	va_start(ap, fmt);
	ux_vprint(stdout, &ux_stdout_bol, fmt, ap);
	va_end(ap);
}

void ux_log(const char *fmt, ...)
{
	va_list ap;

	if (!qdl_debug)
		return;

	ux_clear_line();

	va_start(ap, fmt);
	ux_vprint(stdout, &ux_stdout_bol, fmt, ap);
	va_end(ap);
}

void ux_debug(const char *fmt, ...)
{
	va_list ap;

	if (!qdl_debug)
		return;

	ux_clear_line();

	va_start(ap, fmt);
	ux_vprint(stdout, &ux_stdout_bol, fmt, ap);
	va_end(ap);
}

void ux_progress(const char *fmt, unsigned int value, unsigned int max, ...)
{
	static struct timeval last_progress_update;
	unsigned long elapsed_us;
	unsigned int bar_length;
	unsigned int bars;
	unsigned int dashes;
	struct timeval now;
	char task_name[32];
	float percent;
	va_list ap;

	/* Don't print progress is window is too narrow, or if stdout is redirected */
	if (ux_width < 30)
		return;

	/* Avoid updating the console more than UX_PROGRESS_REFRESH_RATE per second */
	if (last_progress_update.tv_sec) {
		gettimeofday(&now, NULL);
		elapsed_us = (now.tv_sec - last_progress_update.tv_sec) * 1000000 +
			     (now.tv_usec - last_progress_update.tv_usec);

		if (elapsed_us < (1000000 / UX_PROGRESS_REFRESH_RATE))
			return;
	}

	if (value > max)
		value = max;

	va_start(ap, max);
	vsnprintf(task_name, sizeof(task_name), fmt, ap);
	va_end(ap);

	bar_length = ux_width - (20 + 4 + 6);
	percent = (float)value / max;
	bars = percent * bar_length;
	dashes = bar_length - bars;

	printf("%-20.20s [%.*s%.*s] %1.2f%%%n\r", task_name,
	       bars, progress_hashes,
	       dashes, progress_dashes,
	       percent * 100,
	       &ux_cur_line_length);
	fflush(stdout);

	gettimeofday(&last_progress_update, NULL);
}
