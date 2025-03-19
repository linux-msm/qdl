#include <stdarg.h>

#include <sys/time.h>
#include <unistd.h>

#include "qdl_oscompat.h"
#include "qdl.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define UX_PROGRESS_REFRESH_RATE	10
#define UX_PROGRESS_SIZE_MAX		120

static const char * const progress_hashes = "########################################################################################################################";
static const char * const progress_dashes = "------------------------------------------------------------------------------------------------------------------------";

static unsigned int ux_width;
static unsigned int ux_cur_line_length;

/*
 * Levels of output:
 *
 * error: used to signal errors to the user
 * info: used to inform the user about progress
 * logs: log prints from the device
 * debug: protocol logs
 */

/* Clear ux_cur_line_length characters of the progress bar from the screen */
static void ux_clear_line(void)
{
	if (!ux_cur_line_length)
		return;

	printf("%*s\r", ux_cur_line_length, "");
	fflush(stdout);
	ux_cur_line_length = 0;
}

#ifdef _WIN32
void ux_init(void)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	int rows, cols;
	_set_printf_count_output(1);
	// Get the console screen buffer info
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
		cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
		rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
		ux_width = MIN(cols, UX_PROGRESS_SIZE_MAX);
		printf("Rows: %d, Columns: %d\n", rows, cols);
	}
	else {
		printf("Error getting console screen buffer info\n");
	}
}

#else

void ux_init(void)
{
	
	struct winsize w;
	int ret;

	ret = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	if (!ret)
		ux_width = MIN(w.ws_col, UX_PROGRESS_SIZE_MAX);
}
#endif


void ux_err(const char *fmt, ...)
{
	va_list ap;

	ux_clear_line();

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fflush(stderr);
}

void ux_info(const char *fmt, ...)
{
	va_list ap;

	ux_clear_line();

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

	ux_clear_line();

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

	ux_clear_line();

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
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
