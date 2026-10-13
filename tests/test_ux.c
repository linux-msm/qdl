// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
/*
 * Unit tests for the styled printing in ux.c. The module is included
 * directly to reach ux_vprint(); output goes to a regular file, which is
 * never a terminal, so the auto mode must leave it plain while the always
 * mode forces the escape sequences.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "../src/ux.c"
#include "common.h"

bool qdl_debug;

static char out_path[512];

static void print_to(FILE *fp, bool *bol, enum ux_style style, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ux_vprint(fp, bol, style, fmt, ap);
	va_end(ap);
}

/* Open a fresh output file for one test. */
static FILE *open_out(void)
{
	char dir[256];
	FILE *fp;

	assert_int_equal(test_make_temp_dir(dir, sizeof(dir), "qdl-test-ux"), 0);
	snprintf(out_path, sizeof(out_path), "%s/out.txt", dir);
	fp = fopen(out_path, "w+b");
	assert_non_null(fp);

	return fp;
}

/* Read back everything written to @fp, then close and remove it. */
static char *read_out(FILE *fp)
{
	static char buf[1024];
	size_t n;

	fflush(fp);
	rewind(fp);
	n = fread(buf, 1, sizeof(buf) - 1, fp);
	buf[n] = '\0';
	fclose(fp);
	remove(out_path);

	return buf;
}

static int setup(void **state)
{
	(void)state;
	qdl_debug = false;
	ux_set_color_mode(UX_COLOR_AUTO);

	return 0;
}

static void test_plain_when_not_a_terminal(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	(void)state;

	/* Auto mode: a regular file is not a terminal, so no escapes. */
	print_to(fp, &bol, UX_STYLE_ERROR, "boom %d\n", 7);
	assert_string_equal(read_out(fp), "boom 7\n");
}

static void test_never(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	(void)state;

	ux_set_color_mode(UX_COLOR_NEVER);
	print_to(fp, &bol, UX_STYLE_WARNING, "careful\n");
	assert_string_equal(read_out(fp), "careful\n");
}

static void test_always_styles_each_line(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	(void)state;

	ux_set_color_mode(UX_COLOR_ALWAYS);
	print_to(fp, &bol, UX_STYLE_ERROR, "one\ntwo\n\nthree\n");
	assert_string_equal(read_out(fp),
			    "\033[31mone\033[0m\n"
			    "\033[31mtwo\033[0m\n"
			    "\n"
			    "\033[31mthree\033[0m\n");
}

static void test_always_continuation(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	(void)state;

	ux_set_color_mode(UX_COLOR_ALWAYS);
	print_to(fp, &bol, UX_STYLE_LOG, "abc");
	assert_false(bol);
	print_to(fp, &bol, UX_STYLE_LOG, "def\n");
	assert_true(bol);
	assert_string_equal(read_out(fp),
			    "\033[36mabc\033[0m\033[36mdef\033[0m\n");
}

static void test_no_style_stays_plain(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	(void)state;

	ux_set_color_mode(UX_COLOR_ALWAYS);
	print_to(fp, &bol, UX_STYLE_NONE, "info\n");
	assert_string_equal(read_out(fp), "info\n");
}

static void test_debug_prefix_alignment(void **state)
{
	const char *pad = "             "; /* strlen("00:00:00.000 ") */
	const char *second;
	bool bol = true;
	FILE *fp = open_out();
	char *out;
	(void)state;

	qdl_debug = true;
	ux_set_color_mode(UX_COLOR_ALWAYS);
	print_to(fp, &bol, UX_STYLE_NONE, "first\nsecond\n");
	out = read_out(fp);

	/* The timestamp is dimmed... */
	assert_memory_equal(out, "\033[2m", 4);
	assert_non_null(strstr(out, "\033[0m first\n"));

	/* ...and continuation lines are padded to its width on screen. */
	second = strchr(out, '\n') + 1;
	assert_memory_equal(second, pad, strlen(pad));
	assert_string_equal(second + strlen(pad), "second\n");
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_plain_when_not_a_terminal, setup),
		cmocka_unit_test_setup(test_never, setup),
		cmocka_unit_test_setup(test_always_styles_each_line, setup),
		cmocka_unit_test_setup(test_always_continuation, setup),
		cmocka_unit_test_setup(test_no_style_stays_plain, setup),
		cmocka_unit_test_setup(test_debug_prefix_alignment, setup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
