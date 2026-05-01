// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#define _FILE_OFFSET_BITS 64
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700

#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>
#include <libxml/parser.h>

static xmlDocPtr mock_xmlReadFile(const char *filename, const char *encoding, int options);

#define xmlReadFile mock_xmlReadFile
#include "../contents.c"
#undef xmlReadFile

bool qdl_debug;
static int mock_load_sahara_ret = -1;
static int mock_decode_sahara_ret = -1;
static unsigned int mock_load_call_count;
static unsigned int mock_decode_call_count;
static char mock_last_load_filename[PATH_MAX];

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

	mock_load_call_count++;
	snprintf(mock_last_load_filename, sizeof(mock_last_load_filename), "%s", filename);

	if (image) {
		image->name = mock_last_load_filename;
		image->ptr = NULL;
		image->len = 0;
	}

	return mock_load_sahara_ret;
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

	mock_decode_call_count++;
	return mock_decode_sahara_ret;
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

struct firehose_op *firehose_alloc_op(int type)
{
	struct firehose_op *op = calloc(1, sizeof(*op));

	if (!op)
		return NULL;

	op->type = type;
	return op;
}

#define TEST_ROOT "/mock"
#define TEST_CONTENTS_XML TEST_ROOT "/contents.xml"
#define TEST_INVALID_XML TEST_ROOT "/not-contents.xml"

struct xml_fixture {
	const char *root;
	const char *contents_xml;
	const char *invalid_xml;
};

static const char contents_xml[] =
	"<?xml version=\"1.0\"?>"
	"<contents>"
	"  <product_flavors>"
	"    <pf><name>flavor_a</name></pf>"
	"    <pf><name>flavor_b</name></pf>"
	"  </product_flavors>"
	"  <builds_flat>"
	"    <build>"
	"      <linux_root_path>./root/</linux_root_path>"
	"      <file_ref firehose_type=\"true\">"
	"        <file_name>programmer.xml</file_name>"
	"        <file_path>common/build/</file_path>"
	"      </file_ref>"
	"      <device_programmer storage_type=\"ufs\">"
	"        <file_name>prog_firehose.elf</file_name>"
	"        <file_path>boot/</file_path>"
	"      </device_programmer>"
	"      <download_file storage_type=\"ufs\">"
	"        <file_name>payload.bin</file_name>"
	"        <file_path flavor=\"flavor_a\">images/</file_path>"
	"      </download_file>"
	"      <partition_file storage_type=\"ufs\">"
	"        <file_name>rawprogram0.xml</file_name>"
	"        <file_path flavor=\"flavor_a\">ufs/${variant:raw}/</file_path>"
	"      </partition_file>"
	"      <partition_patch_file storage_type=\"ufs\">"
	"        <file_name>patch0.xml</file_name>"
	"        <file_path>ufs/</file_path>"
	"      </partition_patch_file>"
	"      <partition_file storage_type=\"spinor\">"
	"        <file_name>spinor_rawprogram.xml</file_name>"
	"        <file_path flavor=\"flavor_b\">spinor/</file_path>"
	"      </partition_file>"
	"      <download_file storage_type=\"ufs\" ignore=\"true\">"
	"        <file_name>ignored.bin</file_name>"
	"        <file_path>ignored/</file_path>"
	"      </download_file>"
	"      <download_file storage_type=\"ufs\">"
	"        <file_name>*.bin</file_name>"
	"        <file_path>wildcard/</file_path>"
	"      </download_file>"
	"    </build>"
	"  </builds_flat>"
	"</contents>";

static const char contents_xml_device_programmer_only[] =
	"<?xml version=\"1.0\"?>"
	"<contents>"
	"  <builds_flat>"
	"    <build>"
	"      <linux_root_path>./root/</linux_root_path>"
	"      <device_programmer firehose_type=\"lite\">"
	"        <file_name>prog_firehose_lite.elf</file_name>"
	"        <file_path>boot/</file_path>"
	"      </device_programmer>"
	"      <device_programmer>"
	"        <file_name>prog_firehose_ddr.elf</file_name>"
	"        <file_path>boot/</file_path>"
	"      </device_programmer>"
	"    </build>"
	"  </builds_flat>"
	"</contents>";

static const char contents_xml_device_programmer_lite_only[] =
	"<?xml version=\"1.0\"?>"
	"<contents>"
	"  <builds_flat>"
	"    <build>"
	"      <linux_root_path>./root/</linux_root_path>"
	"      <device_programmer firehose_type=\"lite\">"
	"        <file_name>prog_firehose_lite.elf</file_name>"
	"        <file_path>boot/</file_path>"
	"      </device_programmer>"
	"    </build>"
	"  </builds_flat>"
	"</contents>";

static const char contents_xml_storage_only[] =
	"<?xml version=\"1.0\"?>"
	"<contents>"
	"  <builds_flat>"
	"    <build>"
	"      <linux_root_path>./root/</linux_root_path>"
	"      <partition_file storage_type=\"ufs\">"
	"        <file_name>rawprogram0.xml</file_name>"
	"        <file_path>common/build/ufs/</file_path>"
	"      </partition_file>"
	"      <partition_patch_file storage_type=\"ufs\">"
	"        <file_name>patch0.xml</file_name>"
	"        <file_path>common/build/ufs/</file_path>"
	"      </partition_patch_file>"
	"    </build>"
	"  </builds_flat>"
	"</contents>";

static const char *mock_contents_xml_text;
static const char *mock_invalid_xml_text;

static xmlDocPtr mock_xmlReadFile(const char *filename, const char *encoding, int options)
{
	const char *xml;

	(void)encoding;

	if (!strcmp(filename, TEST_CONTENTS_XML))
		xml = mock_contents_xml_text;
	else if (!strcmp(filename, TEST_INVALID_XML))
		xml = mock_invalid_xml_text;
	else
		return NULL;

	return xml ? xmlReadMemory(xml, strlen(xml), filename, NULL, options) : NULL;
}

static void reset_programmer_mocks(void)
{
	mock_load_sahara_ret = -1;
	mock_decode_sahara_ret = -1;
	mock_load_call_count = 0;
	mock_decode_call_count = 0;
	mock_last_load_filename[0] = '\0';
}

static int setup_xml_fixture(void **state)
{
	struct xml_fixture *fixture;

	fixture = calloc(1, sizeof(*fixture));
	assert_non_null(fixture);

	fixture->root = TEST_ROOT;
	fixture->contents_xml = TEST_CONTENTS_XML;
	fixture->invalid_xml = TEST_INVALID_XML;
	mock_contents_xml_text = contents_xml;
	mock_invalid_xml_text = "<not_contents/>\n";
	reset_programmer_mocks();

	*state = fixture;
	return 0;
}

static int teardown_xml_fixture(void **state)
{
	free(*state);
	return 0;
}

static void init_contents(struct contents *contents)
{
	list_init(&contents->entries);
}

static void free_contents(struct contents *contents)
{
	struct contents_entry *entry;
	struct contents_entry *next;
	size_t flavor_idx;

	list_for_each_entry_safe(entry, next, &contents->entries, node) {
		list_del(&entry->node);
		free(entry->filename);
		free(entry->flavor);
		free(entry);
	}

	for (flavor_idx = 0; flavor_idx < contents->num_flavors; flavor_idx++)
		free(contents->flavors[flavor_idx]);
	free(contents->flavors);
}

static size_t count_entries(struct contents *contents)
{
	struct contents_entry *entry;
	size_t count = 0;

	list_for_each_entry(entry, &contents->entries, node)
		count++;

	return count;
}

static struct contents_entry *find_entry(struct contents *contents, const char *filename)
{
	struct contents_entry *entry;

	list_for_each_entry(entry, &contents->entries, node) {
		if (!strcmp(entry->filename, filename))
			return entry;
	}

	return NULL;
}

static void assert_entry(struct xml_fixture *fixture, struct contents *contents,
			 const char *filename, enum contents_file_type file_type,
			 enum qdl_storage_type storage_type, const char *flavor,
			 enum firehose_type firehose_type, const char *relative_path)
{
	struct contents_entry *entry;
	char expected_path[PATH_MAX];
	int ret;

	entry = find_entry(contents, filename);
	assert_non_null(entry);
	assert_int_equal(entry->file_type, file_type);
	assert_int_equal(entry->storage_type, storage_type);
	if (flavor)
		assert_string_equal(entry->flavor, flavor);
	else
		assert_null(entry->flavor);
	assert_int_equal(entry->firehose_type, firehose_type);

	ret = snprintf(expected_path, sizeof(expected_path), "%s/root/%s",
		       fixture->root, relative_path);
	assert_true(ret > 0 && (size_t)ret < sizeof(expected_path));
	assert_string_equal(qdl_pathbuf_str(&entry->path), expected_path);
}

static void test_load_xml_parses_product_flavors(void **state)
{
	struct xml_fixture *fixture = *state;
	struct contents contents = {};

	init_contents(&contents);
	assert_int_equal(contents_load_xml(&contents, fixture->contents_xml), 0);
	assert_int_equal(contents.num_flavors, 2);
	assert_string_equal(contents.flavors[0], "flavor_a");
	assert_string_equal(contents.flavors[1], "flavor_b");
	assert_string_equal(qdl_pathbuf_str(&contents.base_dir), fixture->root);

	free_contents(&contents);
}

static void test_load_xml_parses_file_entries(void **state)
{
	struct xml_fixture *fixture = *state;
	struct contents contents = {};

	init_contents(&contents);
	assert_int_equal(contents_load_xml(&contents, fixture->contents_xml), 0);
	assert_int_equal(count_entries(&contents), 6);

	assert_entry(fixture, &contents, "programmer.xml",
		     CONTENTS_FILE_PROGRAMMER_XML, QDL_STORAGE_UNKNOWN, NULL, 2,
		     "common/build/programmer.xml");
	assert_entry(fixture, &contents, "prog_firehose.elf",
		     CONTENTS_FILE_DEVICE_PROGRAMMER, QDL_STORAGE_UFS, NULL,
		     0, "boot/prog_firehose.elf");
	assert_entry(fixture, &contents, "payload.bin",
		     CONTENTS_FILE_OTHER, QDL_STORAGE_UFS, "flavor_a", 0,
		     "images/payload.bin");
	assert_entry(fixture, &contents, "rawprogram0.xml",
		     CONTENTS_FILE_PROGRAM, QDL_STORAGE_UFS, "flavor_a", 0,
		     "ufs/raw/rawprogram0.xml");
	assert_entry(fixture, &contents, "patch0.xml",
		     CONTENTS_FILE_PATCH, QDL_STORAGE_UFS, NULL, 0,
		     "ufs/patch0.xml");
	assert_entry(fixture, &contents, "spinor_rawprogram.xml",
		     CONTENTS_FILE_PROGRAM, QDL_STORAGE_SPINOR, "flavor_b", 0,
		     "spinor/spinor_rawprogram.xml");

	free_contents(&contents);
}

static void test_load_xml_skips_ignored_and_wildcards(void **state)
{
	struct xml_fixture *fixture = *state;
	struct contents contents = {};

	init_contents(&contents);
	assert_int_equal(contents_load_xml(&contents, fixture->contents_xml), 0);
	assert_null(find_entry(&contents, "ignored.bin"));
	assert_null(find_entry(&contents, "*.bin"));

	free_contents(&contents);
}

static void test_load_xml_rejects_non_contents_document(void **state)
{
	struct xml_fixture *fixture = *state;
	struct contents contents = {};

	init_contents(&contents);
	assert_int_equal(contents_load_xml(&contents, fixture->invalid_xml), -1);
}

static void test_find_programmers_prefers_programmer_xml(void **state)
{
	struct xml_fixture *fixture = *state;
	struct sahara_image images[MAPPING_SZ] = {};
	struct contents contents = {};

	mock_load_sahara_ret = 0;
	mock_decode_sahara_ret = 1;

	init_contents(&contents);
	assert_int_equal(contents_load_xml(&contents, fixture->contents_xml), 0);
	assert_int_equal(contents_find_programmers(&contents, images), 0);
	assert_int_equal(mock_decode_call_count, 1);
	assert_int_equal(mock_load_call_count, 1);
	assert_non_null(strstr(mock_last_load_filename, "programmer.xml"));

	free_contents(&contents);
}

static void test_find_programmers_falls_back_to_non_lite_device_programmer(void **state)
{
	struct xml_fixture *fixture = *state;
	struct sahara_image images[MAPPING_SZ] = {};
	struct contents contents = {};

	mock_contents_xml_text = contents_xml_device_programmer_only;
	mock_load_sahara_ret = 0;

	init_contents(&contents);
	assert_int_equal(contents_load_xml(&contents, fixture->contents_xml), 0);
	assert_int_equal(contents_find_programmers(&contents, images), 0);
	assert_int_equal(mock_decode_call_count, 0);
	assert_int_equal(mock_load_call_count, 1);
	assert_non_null(strstr(mock_last_load_filename, "prog_firehose_ddr.elf"));

	free_contents(&contents);
}

static void test_find_programmers_rejects_lite_only_device_programmer(void **state)
{
	struct xml_fixture *fixture = *state;
	struct sahara_image images[MAPPING_SZ] = {};
	struct contents contents = {};

	mock_contents_xml_text = contents_xml_device_programmer_lite_only;
	mock_load_sahara_ret = 0;

	init_contents(&contents);
	assert_int_equal(contents_load_xml(&contents, fixture->contents_xml), 0);
	assert_int_equal(contents_find_programmers(&contents, images), -1);
	assert_int_equal(mock_decode_call_count, 0);
	assert_int_equal(mock_load_call_count, 0);

	free_contents(&contents);
}

static void test_decode_selectors_accepts_storage_only_contents(void **state)
{
	struct xml_fixture *fixture = *state;
	struct contents_selector *selectors = NULL;
	struct contents contents = {};
	char pattern[] = "ufs";

	mock_contents_xml_text = contents_xml_storage_only;

	init_contents(&contents);
	assert_int_equal(contents_load_xml(&contents, fixture->contents_xml), 0);
	assert_int_equal(contents_decode_selectors(&contents, pattern, &selectors), 1);
	assert_non_null(selectors);
	assert_int_equal(selectors[0].storage_type, QDL_STORAGE_UFS);
	assert_null(selectors[0].flavor);

	free(selectors);
	free_contents(&contents);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(test_load_xml_parses_product_flavors,
						setup_xml_fixture, teardown_xml_fixture),
		cmocka_unit_test_setup_teardown(test_load_xml_parses_file_entries,
						setup_xml_fixture, teardown_xml_fixture),
		cmocka_unit_test_setup_teardown(test_load_xml_skips_ignored_and_wildcards,
						setup_xml_fixture, teardown_xml_fixture),
		cmocka_unit_test_setup_teardown(test_load_xml_rejects_non_contents_document,
						setup_xml_fixture, teardown_xml_fixture),
		cmocka_unit_test_setup_teardown(test_find_programmers_prefers_programmer_xml,
						setup_xml_fixture, teardown_xml_fixture),
		cmocka_unit_test_setup_teardown(test_find_programmers_falls_back_to_non_lite_device_programmer,
						setup_xml_fixture, teardown_xml_fixture),
		cmocka_unit_test_setup_teardown(test_find_programmers_rejects_lite_only_device_programmer,
						setup_xml_fixture, teardown_xml_fixture),
		cmocka_unit_test_setup_teardown(test_decode_selectors_accepts_storage_only_contents,
						setup_xml_fixture, teardown_xml_fixture),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
