// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "common.h"
#include "../src/contents.c"

bool qdl_debug;

void ux_err(const char *fmt, ...)
{
	(void)fmt;
}

void ux_info(const char *fmt, ...)
{
	(void)fmt;
}

void ux_debug(const char *fmt, ...)
{
	(void)fmt;
}

enum qdl_storage_type decode_storage_type(const char *storage)
{
	if (!strcmp(storage, "emmc"))
		return QDL_STORAGE_EMMC;
	if (!strcmp(storage, "nand"))
		return QDL_STORAGE_NAND;
	if (!strcmp(storage, "ufs"))
		return QDL_STORAGE_UFS;
	if (!strcmp(storage, "nvme"))
		return QDL_STORAGE_NVME;
	if (!strcmp(storage, "spinor"))
		return QDL_STORAGE_SPINOR;

	return QDL_STORAGE_UNKNOWN;
}

const char *encode_storage_type(enum qdl_storage_type storage)
{
	switch (storage) {
	case QDL_STORAGE_EMMC:
		return "emmc";
	case QDL_STORAGE_NAND:
		return "nand";
	case QDL_STORAGE_UFS:
		return "ufs";
	case QDL_STORAGE_NVME:
		return "nvme";
	case QDL_STORAGE_SPINOR:
		return "spinor";
	case QDL_STORAGE_UNKNOWN:
	default:
		return NULL;
	}
}

int load_sahara_image(struct qdl_zip *zip, const char *filename, struct sahara_image *image)
{
	(void)zip;
	(void)filename;
	(void)image;

	return -1;
}

void sahara_images_free(struct sahara_image *images, size_t count)
{
	(void)images;
	(void)count;
}

int decode_sahara_config(struct sahara_image *blob, struct sahara_image *images,
			 struct contents_filter *contents_filter)
{
	(void)blob;
	(void)images;
	(void)contents_filter;

	return -1;
}

int program_load(struct list_head *ops, const char *program_file, bool is_nand,
		 bool allow_missing, struct contents_filter *contents_filter,
		 const char *incdir)
{
	(void)ops;
	(void)program_file;
	(void)is_nand;
	(void)allow_missing;
	(void)contents_filter;
	(void)incdir;

	return -1;
}

int patch_load(struct list_head *ops, const char *patch_file)
{
	(void)ops;
	(void)patch_file;

	return -1;
}

struct selector_fixture {
	struct contents contents;
	char *flavors[3];
};

static void add_entry(struct contents *contents, enum contents_file_type file_type,
		      enum qdl_storage_type storage_type, const char *flavor)
{
	struct contents_entry *entry;

	entry = calloc(1, sizeof(*entry));
	assert_non_null(entry);

	entry->file_type = file_type;
	entry->storage_type = storage_type;
	entry->flavor = flavor ? strdup(flavor) : NULL;
	assert_true(!flavor || entry->flavor);

	list_append(&contents->entries, &entry->node);
}

static int setup_single(void **state)
{
	struct selector_fixture *fixture;

	fixture = calloc(1, sizeof(*fixture));
	assert_non_null(fixture);

	fixture->flavors[0] = "flavor_a";

	list_init(&fixture->contents.entries);
	fixture->contents.flavors = fixture->flavors;
	fixture->contents.num_flavors = 1;

	add_entry(&fixture->contents, CONTENTS_FILE_PROGRAM, QDL_STORAGE_UFS, "flavor_a");
	add_entry(&fixture->contents, CONTENTS_FILE_PROGRAM, QDL_STORAGE_UFS, NULL);
	add_entry(&fixture->contents, CONTENTS_FILE_PATCH, QDL_STORAGE_UFS, NULL);
	add_entry(&fixture->contents, CONTENTS_FILE_OTHER, QDL_STORAGE_UNKNOWN, "flavor_a");

	*state = fixture;
	return 0;
}

static int setup_multi(void **state)
{
	struct selector_fixture *fixture;

	fixture = calloc(1, sizeof(*fixture));
	assert_non_null(fixture);

	fixture->flavors[0] = "flavor_a";
	fixture->flavors[1] = "flavor_b";
	fixture->flavors[2] = "flavor_c";

	list_init(&fixture->contents.entries);
	fixture->contents.flavors = fixture->flavors;
	fixture->contents.num_flavors = 3;

	add_entry(&fixture->contents, CONTENTS_FILE_PROGRAM, QDL_STORAGE_UFS, "flavor_a");
	add_entry(&fixture->contents, CONTENTS_FILE_PATCH, QDL_STORAGE_UFS, "flavor_a");
	add_entry(&fixture->contents, CONTENTS_FILE_PROGRAM, QDL_STORAGE_SPINOR, "flavor_b");
	add_entry(&fixture->contents, CONTENTS_FILE_PATCH, QDL_STORAGE_SPINOR, "flavor_b");
	add_entry(&fixture->contents, CONTENTS_FILE_PROGRAM, QDL_STORAGE_SPINOR, "flavor_c");
	add_entry(&fixture->contents, CONTENTS_FILE_PATCH, QDL_STORAGE_SPINOR, "flavor_c");
	add_entry(&fixture->contents, CONTENTS_FILE_OTHER, QDL_STORAGE_UNKNOWN, "flavor_a");

	*state = fixture;
	return 0;
}

static int teardown_fixture(void **state)
{
	struct selector_fixture *fixture = *state;
	struct contents_entry *entry;
	struct contents_entry *next;

	if (!fixture)
		return 0;

	list_for_each_entry_safe(entry, next, &fixture->contents.entries, node) {
		list_del(&entry->node);
		free(entry->flavor);
		free(entry);
	}

	free(fixture);
	return 0;
}

static void assert_selector(struct contents_selector *selector,
			    enum qdl_storage_type storage_type, const char *flavor)
{
	assert_int_equal(selector->storage_type, storage_type);
	assert_string_equal(selector->flavor, flavor);
}

static void assert_decode_success(struct contents *contents, const char *pattern,
				  int expected_count,
				  const struct contents_selector *expected)
{
	struct contents_selector *selectors = NULL;
	char *pattern_copy = NULL;
	int ret;
	int i;

	if (pattern) {
		pattern_copy = strdup(pattern);
		assert_non_null(pattern_copy);
	}

	ret = contents_decode_selectors(contents, pattern_copy, &selectors);
	assert_int_equal(ret, expected_count);
	assert_non_null(selectors);

	for (i = 0; i < expected_count; i++)
		assert_selector(&selectors[i], expected[i].storage_type, expected[i].flavor);

	free(selectors);
	free(pattern_copy);
}

static void assert_decode_failure(struct contents *contents, const char *pattern)
{
	struct contents_selector *selectors = NULL;
	char *pattern_copy = NULL;
	int ret;

	if (pattern) {
		pattern_copy = strdup(pattern);
		assert_non_null(pattern_copy);
	}

	ret = contents_decode_selectors(contents, pattern_copy, &selectors);
	assert_int_equal(ret, -1);
	assert_null(selectors);

	free(pattern_copy);
}

static void test_single_autoselects_only_combination(void **state)
{
	struct selector_fixture *fixture = *state;
	const struct contents_selector expected[] = {
		{ QDL_STORAGE_UFS, "flavor_a" },
	};

	assert_decode_success(&fixture->contents, NULL, 1, expected);
}

static void test_single_accepts_storage_shorthand(void **state)
{
	struct selector_fixture *fixture = *state;
	const struct contents_selector expected[] = {
		{ QDL_STORAGE_UFS, "flavor_a" },
	};

	assert_decode_success(&fixture->contents, "ufs", 1, expected);
}

static void test_single_accepts_flavor_shorthand(void **state)
{
	struct selector_fixture *fixture = *state;
	const struct contents_selector expected[] = {
		{ QDL_STORAGE_UFS, "flavor_a" },
	};

	assert_decode_success(&fixture->contents, "flavor_a", 1, expected);
}

static void test_multi_requires_explicit_selector(void **state)
{
	struct selector_fixture *fixture = *state;

	assert_decode_failure(&fixture->contents, NULL);
}

static void test_multi_accepts_full_multi_selector(void **state)
{
	struct selector_fixture *fixture = *state;
	const struct contents_selector expected[] = {
		{ QDL_STORAGE_UFS, "flavor_a" },
		{ QDL_STORAGE_SPINOR, "flavor_b" },
	};

	assert_decode_success(&fixture->contents,
			      "ufs/flavor_a,spinor/flavor_b",
			      2, expected);
}

static void test_multi_accepts_storage_shorthand_when_unique(void **state)
{
	struct selector_fixture *fixture = *state;
	const struct contents_selector expected[] = {
		{ QDL_STORAGE_UFS, "flavor_a" },
	};

	assert_decode_success(&fixture->contents, "ufs", 1, expected);
}

static void test_multi_accepts_flavor_shorthand_when_unique(void **state)
{
	struct selector_fixture *fixture = *state;
	const struct contents_selector expected[] = {
		{ QDL_STORAGE_SPINOR, "flavor_b" },
	};

	assert_decode_success(&fixture->contents, "flavor_b", 1, expected);
}

static void test_multi_accepts_mixed_shorthand_selector(void **state)
{
	struct selector_fixture *fixture = *state;
	const struct contents_selector expected[] = {
		{ QDL_STORAGE_UFS, "flavor_a" },
		{ QDL_STORAGE_SPINOR, "flavor_b" },
	};

	assert_decode_success(&fixture->contents, "ufs,spinor/flavor_b", 2, expected);
}

static void test_multi_rejects_ambiguous_storage_shorthand(void **state)
{
	struct selector_fixture *fixture = *state;

	assert_decode_failure(&fixture->contents, "spinor");
}

static void test_multi_rejects_invalid_combination(void **state)
{
	struct selector_fixture *fixture = *state;

	assert_decode_failure(&fixture->contents, "ufs/flavor_b");
}

static void test_multi_rejects_duplicate_storage_full_selectors(void **state)
{
	struct selector_fixture *fixture = *state;

	assert_decode_failure(&fixture->contents, "spinor/flavor_b,spinor/flavor_c");
}

static void test_multi_rejects_duplicate_storage_after_shorthand(void **state)
{
	struct selector_fixture *fixture = *state;

	assert_decode_failure(&fixture->contents, "flavor_b,spinor/flavor_c");
}

static void test_multi_rejects_unknown_selector(void **state)
{
	struct selector_fixture *fixture = *state;

	assert_decode_failure(&fixture->contents, "does_not_exist");
}

static void test_multi_rejects_empty_selector(void **state)
{
	struct selector_fixture *fixture = *state;

	assert_decode_failure(&fixture->contents, "");
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(test_single_autoselects_only_combination,
						setup_single, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_single_accepts_storage_shorthand,
						setup_single, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_single_accepts_flavor_shorthand,
						setup_single, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_requires_explicit_selector,
						setup_multi, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_accepts_full_multi_selector,
						setup_multi, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_accepts_storage_shorthand_when_unique,
						setup_multi, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_accepts_flavor_shorthand_when_unique,
						setup_multi, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_accepts_mixed_shorthand_selector,
						setup_multi, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_rejects_ambiguous_storage_shorthand,
						setup_multi, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_rejects_invalid_combination,
						setup_multi, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_rejects_duplicate_storage_full_selectors,
						setup_multi, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_rejects_duplicate_storage_after_shorthand,
						setup_multi, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_rejects_unknown_selector,
						setup_multi, teardown_fixture),
		cmocka_unit_test_setup_teardown(test_multi_rejects_empty_selector,
						setup_multi, teardown_fixture),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
