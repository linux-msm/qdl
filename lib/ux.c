// SPDX-License-Identifier: BSD-3-Clause
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#endif
#include <sys/time.h>
#include <unistd.h>

#include "qdl.h"

#define UX_PROGRESS_REFRESH_RATE	10
#define UX_PROGRESS_SIZE_MAX		80

#define HASHES "################################################################################"
#define DASHES "--------------------------------------------------------------------------------"

static const char * const progress_hashes = HASHES;
static const char * const progress_dashes = DASHES;

static unsigned int ux_width;
static unsigned int ux_cur_line_length;

/* Clear ux_cur_line_length characters of the progress bar from the screen */
static void term_clear_line(void)
{
	if (!ux_cur_line_length)
		return;

	printf("%*s\r", ux_cur_line_length, "");
	fflush(stdout);
	ux_cur_line_length = 0;
}

static void term_err(const char *fmt, va_list ap)
{
	term_clear_line();
	vfprintf(stderr, fmt, ap);
	fflush(stderr);
}

static void term_info(const char *fmt, va_list ap)
{
	term_clear_line();
	vprintf(fmt, ap);
	fflush(stdout);
}

static void term_progress(const char *fmt, unsigned int value,
			   unsigned int max, va_list ap)
{
	static struct timeval last_progress_update;
	unsigned long elapsed_us;
	unsigned int bar_length;
	unsigned int bars;
	unsigned int dashes;
	struct timeval now;
	char task_name[32];
	float percent;

	/* Don't print progress if window is too narrow, or if stdout is redirected */
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

	vsnprintf(task_name, sizeof(task_name), fmt, ap);

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

static const struct qdl_ux_ops terminal_ops = {
	.err      = term_err,
	.info     = term_info,
	.log      = term_info,
	.debug    = term_info,
	.progress = term_progress,
};

static const struct qdl_ux_ops *ux_ops = &terminal_ops;

#ifdef _WIN32

static void term_init(void)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	int columns;

	HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);

	if (GetConsoleScreenBufferInfo(stdoutHandle, &csbi)) {
		columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
		ux_width = MIN(columns, UX_PROGRESS_SIZE_MAX);
	}
}

#else

static void term_init(void)
{
	struct winsize w;
	int ret;

	ret = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	if (!ret)
		ux_width = MIN(w.ws_col, UX_PROGRESS_SIZE_MAX);
}

#endif

void qdl_ux_set_ops(const struct qdl_ux_ops *ops)
{
	if (ops) {
		ux_ops = ops;
	} else {
		term_init();
		ux_ops = &terminal_ops;
	}
}

void ux_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ux_ops->err(fmt, ap);
	va_end(ap);
}

void ux_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ux_ops->info(fmt, ap);
	va_end(ap);
}

void ux_log(const char *fmt, ...)
{
	va_list ap;

	if (!qdl_debug)
		return;

	va_start(ap, fmt);
	ux_ops->log(fmt, ap);
	va_end(ap);
}

void ux_debug(const char *fmt, ...)
{
	va_list ap;

	if (!qdl_debug)
		return;

	va_start(ap, fmt);
	ux_ops->debug(fmt, ap);
	va_end(ap);
}

void ux_progress(const char *fmt, unsigned int value, unsigned int max, ...)
{
	va_list ap;

	va_start(ap, max);
	ux_ops->progress(fmt, value, max, ap);
	va_end(ap);
}
