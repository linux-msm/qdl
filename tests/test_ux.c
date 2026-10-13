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
	ux_vprint(fp, bol, style, UX_LEVEL_NONE, UX_TAG_NONE, fmt, ap);
	va_end(ap);
}

static void print_tagged(FILE *fp, bool *bol, enum ux_style style, enum ux_tag tag,
			 const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ux_vprint(fp, bol, style, UX_LEVEL_NONE, tag, fmt, ap);
	va_end(ap);
}

static void print_level(FILE *fp, bool *bol, enum ux_style style, enum ux_level level,
			enum ux_tag tag, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ux_vprint(fp, bol, style, level, tag, fmt, ap);
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

static void test_tag_plain(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	(void)state;

	/* Without color the tag is the same text the messages used to carry. */
	print_tagged(fp, &bol, UX_STYLE_ERROR, UX_TAG_SAHARA, "bad packet %d\n", 3);
	print_tagged(fp, &bol, UX_STYLE_NONE, UX_TAG_FIREHOSE_RAW, "WRITE: x\n");
	assert_string_equal(read_out(fp),
			    "SAHARA: bad packet 3\n"
			    "FIREHOSE_RAW: WRITE: x\n");
}

static void test_tag_colored_once(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	(void)state;

	/* The tag keeps its own color and leads only the first line. */
	ux_set_color_mode(UX_COLOR_ALWAYS);
	print_tagged(fp, &bol, UX_STYLE_ERROR, UX_TAG_VIP, "one\ntwo\n");
	assert_string_equal(read_out(fp),
			    "\033[1;32mVIP:\033[0m \033[31mone\033[0m\n"
			    "\033[31mtwo\033[0m\n");
}

static void test_tag_after_debug_prefix(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	char *out;
	(void)state;

	qdl_debug = true;
	print_tagged(fp, &bol, UX_STYLE_NONE, UX_TAG_USB, "chunk\n");
	out = read_out(fp);

	/* "HH:MM:SS.mmm USB: chunk" */
	assert_int_equal(out[12], ' ');
	assert_string_equal(out + 13, "USB: chunk\n");
}

static void test_level_plain(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	(void)state;

	print_level(fp, &bol, UX_STYLE_ERROR, UX_LEVEL_ERR, UX_TAG_NONE, "boom\n");
	print_level(fp, &bol, UX_STYLE_ERROR, UX_LEVEL_ERR, UX_TAG_SAHARA, "bad packet\n");
	print_level(fp, &bol, UX_STYLE_WARNING, UX_LEVEL_WARN, UX_TAG_NONE, "careful\n");
	assert_string_equal(read_out(fp),
			    "ERR: boom\n"
			    "ERR: SAHARA: bad packet\n"
			    "WARN: careful\n");
}

static void test_level_once_per_line(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	(void)state;

	/* A line assembled from several calls carries the level once. */
	print_level(fp, &bol, UX_STYLE_ERROR, UX_LEVEL_ERR, UX_TAG_NONE, "qdl: ");
	print_level(fp, &bol, UX_STYLE_ERROR, UX_LEVEL_ERR, UX_TAG_NONE, "no device\n");
	print_level(fp, &bol, UX_STYLE_ERROR, UX_LEVEL_ERR, UX_TAG_NONE, "next\n");
	assert_string_equal(read_out(fp), "ERR: qdl: no device\nERR: next\n");
}

static void test_level_colored(void **state)
{
	bool bol = true;
	FILE *fp = open_out();
	(void)state;

	ux_set_color_mode(UX_COLOR_ALWAYS);
	print_level(fp, &bol, UX_STYLE_ERROR, UX_LEVEL_ERR, UX_TAG_USB, "x\n");
	assert_string_equal(read_out(fp),
			    "\033[1;31mERR:\033[0m \033[1;36mUSB:\033[0m \033[31mx\033[0m\n");
}

static void test_quiet_prefix(void **state)
{
	enum ux_tag tag;
	(void)state;

	/* Regular output stays bare outside debug mode, tag or not. */
	tag = UX_TAG_FIREHOSE;
	assert_int_equal(ux_quiet_prefix(UX_LEVEL_INFO, &tag), UX_LEVEL_NONE);
	assert_int_equal(tag, UX_TAG_NONE);

	/* In debug mode the subsystem tag wins, the level fills in for none. */
	qdl_debug = true;
	tag = UX_TAG_FIREHOSE;
	assert_int_equal(ux_quiet_prefix(UX_LEVEL_INFO, &tag), UX_LEVEL_NONE);
	assert_int_equal(tag, UX_TAG_FIREHOSE);

	tag = UX_TAG_NONE;
	assert_int_equal(ux_quiet_prefix(UX_LEVEL_DEBUG, &tag), UX_LEVEL_DEBUG);
	assert_int_equal(tag, UX_TAG_NONE);
}

static void test_format_size(void **state)
{
	char buf[16];
	(void)state;

	assert_string_equal(ux_format_size(buf, sizeof(buf), 0), "0 B");
	assert_string_equal(ux_format_size(buf, sizeof(buf), 1023), "1023 B");
	assert_string_equal(ux_format_size(buf, sizeof(buf), 1024), "1.0 KiB");
	assert_string_equal(ux_format_size(buf, sizeof(buf), 3355443), "3.2 MiB");
	assert_string_equal(ux_format_size(buf, sizeof(buf), 536870912), "512.0 MiB");
	assert_string_equal(ux_format_size(buf, sizeof(buf), 2254857830ULL), "2.1 GiB");
	assert_string_equal(ux_format_size(buf, sizeof(buf), 5ULL << 40), "5.0 TiB");
	/* Beyond the largest unit the value just grows. */
	assert_string_equal(ux_format_size(buf, sizeof(buf), 2048ULL << 40), "2048.0 TiB");
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
		cmocka_unit_test_setup(test_tag_plain, setup),
		cmocka_unit_test_setup(test_tag_colored_once, setup),
		cmocka_unit_test_setup(test_tag_after_debug_prefix, setup),
		cmocka_unit_test_setup(test_level_plain, setup),
		cmocka_unit_test_setup(test_level_once_per_line, setup),
		cmocka_unit_test_setup(test_level_colored, setup),
		cmocka_unit_test_setup(test_quiet_prefix, setup),
		cmocka_unit_test_setup(test_format_size, setup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
