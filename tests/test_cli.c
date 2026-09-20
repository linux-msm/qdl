// SPDX-License-Identifier: BSD-3-Clause
/*
 * Unit tests for qdl_parse_args(). Extracting the parser into cli.c (out of
 * qdl.c, which defines main()) is what makes it testable here. util.c is
 * linked for the real decode_storage_type()/decode_backend(); the ux and
 * usage helpers are stubbed. Only valid inputs are exercised - the unknown
 * storage/backend/skipblock cases errx() out of the process by design.
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

#ifdef _WIN32
const char *__progname = "test_cli";

/* errx() lives in oscompat.c, which the test does not link. */
void errx(int eval, const char *fmt, ...)
{
	(void)fmt;
	exit(eval);
}
#endif

/* --- globals / stubs --- */
bool qdl_debug;

void ux_err(const char *fmt, ...) { (void)fmt; }
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

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_defaults),
		cmocka_unit_test(test_storage_and_backend),
		cmocka_unit_test(test_dry_run_and_skipblock),
		cmocka_unit_test(test_flags_and_values),
		cmocka_unit_test(test_help_version_and_usage),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
