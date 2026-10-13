// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
/*
 * Unit tests for qdl_parse_args(). Extracting the parser into cli.c (out of
 * qdl.c, which defines main()) is what makes it testable here. util.c is
 * linked for the real decode_storage_type()/decode_backend(); the ux and
 * usage helpers are stubbed. Only valid inputs are exercised - the unknown
 * storage/backend/skipblock cases ux_diex() out of the process by design.
 */
#include <getopt.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "qdl.h"
#include "file.h"
#include "cli.h"

/* --- globals / stubs --- */
bool qdl_debug;

void ux_err(const char *fmt, ...) { (void)fmt; }
void ux_info(const char *fmt, ...) { (void)fmt; }
void ux_warnx(const char *fmt, ...) { (void)fmt; }
const char *ux_progname(void) { return "test_cli"; }

/* Record what qdl_take_color_option() applies. */
static enum ux_color_mode last_color_mode;
static int color_mode_calls;

void ux_set_color_mode(enum ux_color_mode mode)
{
	last_color_mode = mode;
	color_mode_calls++;
}

/* Fatal logging lives in ux.c, which the test does not link. */
void __attribute__((noreturn)) ux_diex(int status, const char *fmt, ...)
{
	(void)fmt;
	exit(status);
}

int qdl_file_open(struct qdl_zip *z, const char *f, struct qdl_file *file)
{ (void)z; (void)f; (void)file; return -1; }
void *qdl_file_load(struct qdl_file *file, size_t *len) { (void)file; (void)len; return NULL; }
void qdl_file_close(struct qdl_file *file) { (void)file; }

/*
 * Copy a NULL-terminated argv template into a mutable array (getopt permutes
 * its argv), reset getopt, and parse.
 */
static int parse(struct qdl_opts *opts, const char *const *tmpl)
{
	char *argv[24];
	int argc = 0;

	while (tmpl[argc]) {
		assert_true((size_t)argc < ARRAY_SIZE(argv) - 1);
		argv[argc] = (char *)tmpl[argc];
		argc++;
	}
	argv[argc] = NULL;

	optind = 0; /* force glibc getopt to reinitialise between calls */
	return qdl_parse_args(argc, argv, opts);
}

static void test_defaults(void **state)
{
	static const char *const argv[] = { "qdl", "prog.mbn", "rawprogram.xml", NULL };
	struct qdl_opts o;
	(void)state;

	assert_int_equal(parse(&o, argv), QDL_ARGS_OK);
	assert_int_equal(o.storage_type, QDL_STORAGE_UFS);
	assert_int_equal(o.dev_type, QDL_DEVICE_AUTO);
	assert_int_equal(o.skipblock_mode, QDL_SKIPBLOCK_NONE);
	assert_int_equal(o.slot, UINT_MAX);
	assert_null(o.incdir);
	assert_null(o.serial);
	assert_false(o.allow_missing);
	assert_false(o.allow_fusing);
	assert_false(o.finalize_provisioning);
	assert_false(o.skip_reset);
	assert_int_equal(o.out_chunk_size, 0);
}

static void test_storage_and_backend(void **state)
{
	static const char *const emmc[] = { "qdl", "-s", "emmc", "prog", "f", NULL };
	static const char *const nvme[] = { "qdl", "--storage", "nvme", "prog", "f", NULL };
	static const char *const usb[] = { "qdl", "--backend", "usb", "prog", "f", NULL };
	struct qdl_opts o;
	(void)state;

	assert_int_equal(parse(&o, emmc), QDL_ARGS_OK);
	assert_int_equal(o.storage_type, QDL_STORAGE_EMMC);

	assert_int_equal(parse(&o, nvme), QDL_ARGS_OK);
	assert_int_equal(o.storage_type, QDL_STORAGE_NVME);

	assert_int_equal(parse(&o, usb), QDL_ARGS_OK);
	assert_int_equal(o.dev_type, QDL_DEVICE_USB);
}

static void test_dry_run_and_skipblock(void **state)
{
	static const char *const dry[] = { "qdl", "-n", "prog", "f", NULL };
	static const char *const skip[] = { "qdl", "--skipblock", "sha256", "prog", "f", NULL };
	static const char *const both[] = { "qdl", "-n", "--backend", "usb", "prog", "f", NULL };
	struct qdl_opts o;
	(void)state;

	assert_int_equal(parse(&o, dry), QDL_ARGS_OK);
	assert_int_equal(o.dev_type, QDL_DEVICE_SIM);

	assert_int_equal(parse(&o, skip), QDL_ARGS_OK);
	assert_int_equal(o.skipblock_mode, QDL_SKIPBLOCK_SHA256);

	/* --dry-run pins the backend; a later --backend is ignored. */
	assert_int_equal(parse(&o, both), QDL_ARGS_OK);
	assert_int_equal(o.dev_type, QDL_DEVICE_SIM);
}

static void test_flags_and_values(void **state)
{
	static const char *const argv[] = {
		"qdl", "-f", "-c", "-l", "-R",
		"-i", "incdir", "-S", "0AA94EFD", "-T", "3",
		"-u", "4096", "-D", "vippath", "-t", "vipdir",
		"prog", "f", NULL
	};
	struct qdl_opts o;
	(void)state;

	assert_int_equal(parse(&o, argv), QDL_ARGS_OK);
	assert_true(o.allow_missing);
	assert_true(o.allow_fusing);
	assert_true(o.finalize_provisioning);
	assert_true(o.skip_reset);
	assert_string_equal(o.incdir, "incdir");
	assert_string_equal(o.serial, "0AA94EFD");
	assert_int_equal(o.slot, 3);
	assert_int_equal(o.out_chunk_size, 4096);
	assert_string_equal(o.vip_table_path, "vippath");
	assert_string_equal(o.vip_generate_dir, "vipdir");
	/* --create-digests also enforces dry-run. */
	assert_int_equal(o.dev_type, QDL_DEVICE_SIM);
}

static void test_help_version_and_usage(void **state)
{
	static const char *const ver[] = { "qdl", "--version", NULL };
	static const char *const help[] = { "qdl", "--help", NULL };
	static const char *const too_few[] = { "qdl", "prog", NULL };
	static const char *const none[] = { "qdl", NULL };
	struct qdl_opts o;
	(void)state;

	assert_int_equal(parse(&o, ver), QDL_ARGS_DONE);
	assert_int_equal(parse(&o, help), QDL_ARGS_DONE);

	/* Fewer than two positional arguments is a usage error. */
	assert_int_equal(parse(&o, too_few), QDL_ARGS_ERROR);
	assert_int_equal(parse(&o, none), QDL_ARGS_ERROR);
}

/*
 * Run qdl_take_color_option() on a copy of @tmpl and check the remaining
 * arguments against @expect. Returns the function's result.
 */
static int take_color(const char *const *tmpl, const char *const *expect)
{
	char *argv[16];
	int argc = 0;
	int ret;
	int i;

	while (tmpl[argc]) {
		assert_true((size_t)argc < ARRAY_SIZE(argv) - 1);
		argv[argc] = (char *)tmpl[argc];
		argc++;
	}
	argv[argc] = NULL;

	last_color_mode = UX_COLOR_AUTO;
	color_mode_calls = 0;

	ret = qdl_take_color_option(&argc, argv);
	if (ret < 0)
		return ret;

	for (i = 0; expect[i]; i++) {
		assert_true(i < argc);
		assert_string_equal(argv[i], expect[i]);
	}
	assert_int_equal(argc, i);
	assert_null(argv[argc]);

	return ret;
}

static void test_color_option(void **state)
{
	static const char *const never[] = { "qdl", "--color=never", "prog", "-d", "f", NULL };
	static const char *const never_left[] = { "qdl", "prog", "-d", "f", NULL };
	static const char *const bare[] = { "qdl", "ramdump", "--color", "-o", "out", NULL };
	static const char *const bare_left[] = { "qdl", "ramdump", "-o", "out", NULL };
	static const char *const twice[] = { "qdl", "--color=never", "list", "--color=auto", NULL };
	static const char *const twice_left[] = { "qdl", "list", NULL };
	static const char *const none[] = { "qdl", "--help", NULL };
	(void)state;

	assert_int_equal(take_color(never, never_left), 0);
	assert_int_equal(color_mode_calls, 1);
	assert_int_equal(last_color_mode, UX_COLOR_NEVER);

	/* A bare --color means always, and is found after a subcommand too. */
	assert_int_equal(take_color(bare, bare_left), 0);
	assert_int_equal(last_color_mode, UX_COLOR_ALWAYS);

	/* The last occurrence wins. */
	assert_int_equal(take_color(twice, twice_left), 0);
	assert_int_equal(color_mode_calls, 2);
	assert_int_equal(last_color_mode, UX_COLOR_AUTO);

	assert_int_equal(take_color(none, none), 0);
	assert_int_equal(color_mode_calls, 0);
}

static void test_color_option_edge_cases(void **state)
{
	static const char *const dashdash[] = { "qdl", "--color=never", "--", "--color=always", NULL };
	static const char *const dashdash_left[] = { "qdl", "--", "--color=always", NULL };
	static const char *const bogus[] = { "qdl", "--color=sometimes", "list", NULL };
	static const char *const prefix[] = { "qdl", "--colorful", NULL };
	static const char *const empty[] = { NULL };
	(void)state;

	/* Arguments after "--" are positional and stay untouched. */
	assert_int_equal(take_color(dashdash, dashdash_left), 0);
	assert_int_equal(color_mode_calls, 1);
	assert_int_equal(last_color_mode, UX_COLOR_NEVER);

	assert_int_equal(take_color(bogus, bogus), -1);
	assert_int_equal(color_mode_calls, 0);

	/* Only an exact --color or --color=... is taken. */
	assert_int_equal(take_color(prefix, prefix), 0);
	assert_int_equal(color_mode_calls, 0);

	/* An empty argv, as execve() allows, stays empty. */
	assert_int_equal(take_color(empty, empty), 0);
	assert_int_equal(color_mode_calls, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_defaults),
		cmocka_unit_test(test_storage_and_backend),
		cmocka_unit_test(test_dry_run_and_skipblock),
		cmocka_unit_test(test_flags_and_values),
		cmocka_unit_test(test_help_version_and_usage),
		cmocka_unit_test(test_color_option),
		cmocka_unit_test(test_color_option_edge_cases),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
