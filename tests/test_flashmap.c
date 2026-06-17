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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <cmocka.h>
#include <libxml/parser.h>

#include "file.h"
#include "firehose.h"
#include "flashmap.h"
#include "qdl.h"

#ifdef _WIN32
const char *__progname = "test_flashmap";
#endif

#define FLASHMAP_JSON_FILENAME "flashmap.json"
#define FLASHMAP_MULTI_JSON_FILENAME "flashmap-multi.json"
#define FLASHMAP_ZIP_FILENAME "flashmap.zip"
#define PROGRAMMER_FILENAME "prog_firehose_ddr.elf"
#define PROGRAM0_FILENAME "rawprogram0.xml"
#define PROGRAM1_FILENAME "rawprogram1.xml"
#define PATCH0_FILENAME "patch0.xml"
#define PATCH1_FILENAME "patch1.xml"

bool qdl_debug;

struct qdl_zip {
	bool is_zip;
};

struct mock_file {
	const char *name;
	const char *content;
};

static const char flashmap_json[] =
	"{"
	"  \"version\": \"1.1.0\","
	"  \"products\": ["
	"    {"
	"      \"name\": \"unit-test\","
	"      \"layouts\": ["
	"        {"
	"          \"name\": \"layout0\","
	"          \"programmer\": [\"" PROGRAMMER_FILENAME "\"],"
	"          \"programmable\": ["
	"            {"
	"              \"memory\": \"spinor\","
	"              \"slot\": 0,"
	"              \"files\": ["
	"                \"" PROGRAM0_FILENAME "\","
	"                \"" PROGRAM1_FILENAME "\","
	"                \"" PATCH0_FILENAME "\","
	"                \"" PATCH1_FILENAME "\""
	"              ]"
	"            }"
	"          ]"
	"        }"
	"      ]"
	"    }"
	"  ]"
	"}";

static const char flashmap_multi_json[] =
	"{"
	"  \"version\": \"1.1.0\","
	"  \"products\": ["
	"    {"
	"      \"name\": \"unit-test\","
	"      \"layouts\": ["
	"        {"
	"          \"name\": \"layout0\","
	"          \"programmer\": [\"" PROGRAMMER_FILENAME "\"],"
	"          \"programmable\": ["
	"            {"
	"              \"memory\": \"ufs\","
	"              \"slot\": 0,"
	"              \"files\": [\"" PROGRAM0_FILENAME "\"]"
	"            }"
	"          ]"
	"        },"
	"        {"
	"          \"name\": \"layout1\","
	"          \"programmer\": [\"" PROGRAMMER_FILENAME "\"],"
	"          \"programmable\": ["
	"            {"
	"              \"memory\": \"spinor\","
	"              \"slot\": 0,"
	"              \"files\": ["
	"                \"" PROGRAM0_FILENAME "\","
	"                \"" PROGRAM1_FILENAME "\","
	"                \"" PATCH0_FILENAME "\","
	"                \"" PATCH1_FILENAME "\""
	"              ]"
	"            }"
	"          ]"
	"        }"
	"      ]"
	"    }"
	"  ]"
	"}";

static const char program_xml[] =
	"<data>"
	"  <program SECTOR_SIZE_IN_BYTES=\"512\" filename=\"payload.bin\" label=\"test\" "
	"           num_partition_sectors=\"1\" physical_partition_number=\"0\" start_sector=\"0\"/>"
	"</data>";

static const char patch_xml[] =
	"<patches>"
	"  <patch byte_offset=\"0\" filename=\"DISK\" physical_partition_number=\"0\" "
	"         size_in_bytes=\"4\" start_sector=\"0\" value=\"0\" what=\"zero\"/>"
	"</patches>";

static const struct mock_file mock_files[] = {
	{ FLASHMAP_JSON_FILENAME, flashmap_json },
	{ FLASHMAP_MULTI_JSON_FILENAME, flashmap_multi_json },
	{ PROGRAM0_FILENAME, program_xml },
	{ PROGRAM1_FILENAME, program_xml },
	{ PATCH0_FILENAME, patch_xml },
	{ PATCH1_FILENAME, patch_xml },
};

static char *loaded_programmer;

static const struct mock_file *find_mock_file(const char *filename)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(mock_files); i++) {
		if (!strcmp(mock_files[i].name, filename))
			return &mock_files[i];
	}

	return NULL;
}

static void reset_mock_state(void)
{
	free(loaded_programmer);
	loaded_programmer = NULL;
}

int qdl_zip_open(const char *filename, struct qdl_zip **qzip)
{
	static struct qdl_zip zip = { .is_zip = true };

	if (!strcmp(filename, FLASHMAP_ZIP_FILENAME)) {
		*qzip = &zip;
		return 0;
	}

	*qzip = NULL;
	return 0;
}

struct qdl_zip *qdl_zip_get(struct qdl_zip *qzip)
{
	return qzip;
}

void qdl_zip_put(struct qdl_zip *qzip)
{
	(void)qzip;
}

int qdl_file_open(struct qdl_zip *qzip, const char *filename, struct qdl_file *file)
{
	const struct mock_file *mock_file;

	if (qzip)
		assert_true(qzip->is_zip);

	mock_file = find_mock_file(filename);
	if (!mock_file)
		return -1;

	file->type = qzip ? QDL_FILE_TYPE_ZIP : QDL_FILE_TYPE_POSIX;
	file->fd = (int)(mock_file - mock_files);
	file->zip_file = NULL;
	file->size = strlen(mock_file->content);

	return 0;
}

void *qdl_file_load(struct qdl_file *file, size_t *len)
{
	const struct mock_file *mock_file;
	char *copy;

	assert_true(file->fd >= 0);
	assert_true((size_t)file->fd < ARRAY_SIZE(mock_files));
	mock_file = &mock_files[file->fd];

	*len = strlen(mock_file->content);
	copy = malloc(*len + 1);
	assert_non_null(copy);
	memcpy(copy, mock_file->content, *len + 1);
	return copy;
}

void qdl_file_close(struct qdl_file *file)
{
	file->type = QDL_FILE_TYPE_UNKNOWN;
}

size_t qdl_file_getsize(struct qdl_file *file)
{
	return file->size;
}

ssize_t qdl_file_read(struct qdl_file *file, void *buf, size_t len)
{
	(void)file;
	(void)buf;
	(void)len;
	return -1;
}

ssize_t qdl_file_read_exact(struct qdl_file *file, void *buf, size_t len)
{
	(void)file;
	(void)buf;
	(void)len;
	return -1;
}

off_t qdl_file_seek(struct qdl_file *file, off_t offset, int whence)
{
	(void)file;
	(void)offset;
	(void)whence;
	return -1;
}

int load_sahara_image(struct qdl_zip *zip, const char *filename, struct sahara_image *image)
{
	(void)zip;

	free(loaded_programmer);
	loaded_programmer = strdup(filename);
	assert_non_null(loaded_programmer);

	image->name = strdup(filename);
	assert_non_null(image->name);

	return 0;
}

void sahara_images_free(struct sahara_image *images, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		free(images[i].name);
		images[i].name = NULL;
	}
}

struct firehose_op *firehose_alloc_op(int type)
{
	struct firehose_op *op;

	op = calloc(1, sizeof(*op));
	assert_non_null(op);
	op->type = type;
	return op;
}

void firehose_free_ops(struct list_head *ops)
{
	struct firehose_op *next;
	struct firehose_op *op;

	list_for_each_entry_safe(op, next, ops, node) {
		list_del(&op->node);
		free((void *)op->filename);
		free(op);
	}
}

int program_load_xml(struct list_head *ops, xmlDoc *doc, struct qdl_zip *zip, const char *program_file,
		     bool is_nand, bool allow_missing, struct contents_filter *contents_filter, const char *incdir)
{
	struct firehose_op *op;

	(void)doc;
	(void)zip;
	(void)is_nand;
	(void)allow_missing;
	(void)contents_filter;
	(void)incdir;

	op = firehose_alloc_op(FIREHOSE_OP_PROGRAM);
	op->filename = strdup(program_file);
	assert_non_null(op->filename);
	list_append(ops, &op->node);

	return 0;
}

int patch_load_xml(struct list_head *ops, xmlDoc *doc, const char *patch_file)
{
	struct firehose_op *op;

	(void)doc;

	op = firehose_alloc_op(FIREHOSE_OP_PATCH);
	op->filename = strdup(patch_file);
	assert_non_null(op->filename);
	list_append(ops, &op->node);

	return 0;
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

void ux_progress(const char *fmt, unsigned int value, unsigned int size, ...)
{
	(void)fmt;
	(void)value;
	(void)size;
}

static size_t count_ops(struct list_head *ops, enum firehose_op_type type)
{
	struct firehose_op *op;
	size_t count = 0;

	list_for_each_entry(op, ops, node) {
		if (op->type == type)
			count++;
	}

	return count;
}

static void assert_loads_as(const char *filename, char *specifier,
			    enum qdl_storage_type storage_type,
			    size_t program_count, size_t patch_count)
{
	struct sahara_image images[MAPPING_SZ] = {};
	struct list_head ops = LIST_INIT(ops);
	struct firehose_op *op;
	int ret;

	ret = flashmap_load(&ops, filename, specifier, images, NULL);
	assert_int_equal(ret, 0);
	assert_string_equal(loaded_programmer, PROGRAMMER_FILENAME);
	assert_non_null(images[SAHARA_ID_EHOSTDL_IMG].name);
	assert_string_equal(images[SAHARA_ID_EHOSTDL_IMG].name, PROGRAMMER_FILENAME);

	op = list_entry_first(&ops, struct firehose_op, node);
	assert_int_equal(op->type, FIREHOSE_OP_CONFIGURE);
	assert_int_equal(op->storage_type, storage_type);
	assert_int_equal(count_ops(&ops, FIREHOSE_OP_CONFIGURE), 1);
	assert_int_equal(count_ops(&ops, FIREHOSE_OP_PROGRAM), program_count);
	assert_int_equal(count_ops(&ops, FIREHOSE_OP_PATCH), patch_count);

	firehose_free_ops(&ops);
	sahara_images_free(images, MAPPING_SZ);
}

static void assert_flashmap_loads(const char *filename, char *specifier)
{
	assert_loads_as(filename, specifier, QDL_STORAGE_SPINOR, 2, 2);
}

static void test_flashmap_json_loads(void **state)
{
	(void)state;
	reset_mock_state();
	assert_flashmap_loads(FLASHMAP_JSON_FILENAME, NULL);
	reset_mock_state();
}

static void test_flashmap_zip_loads(void **state)
{
	(void)state;
	reset_mock_state();
	assert_flashmap_loads(FLASHMAP_ZIP_FILENAME, NULL);
	reset_mock_state();
}

static void test_flashmap_storage_filter_loads(void **state)
{
	char specifier[] = "spinor";

	(void)state;
	reset_mock_state();
	assert_flashmap_loads(FLASHMAP_JSON_FILENAME, specifier);
	reset_mock_state();
}

static void test_flashmap_multi_layout_requires_selector(void **state)
{
	struct sahara_image images[MAPPING_SZ] = {};
	struct list_head ops = LIST_INIT(ops);
	int ret;

	(void)state;
	reset_mock_state();

	ret = flashmap_load(&ops, FLASHMAP_MULTI_JSON_FILENAME, NULL, images, NULL);
	assert_int_equal(ret, -1);
	assert_true(list_empty(&ops));
	assert_null(loaded_programmer);

	sahara_images_free(images, MAPPING_SZ);
}

static void test_flashmap_multi_layout_selects_layout(void **state)
{
	char specifier[] = "layout1";

	(void)state;
	reset_mock_state();
	assert_loads_as(FLASHMAP_MULTI_JSON_FILENAME, specifier,
			QDL_STORAGE_SPINOR, 2, 2);
	reset_mock_state();
}

static void test_flashmap_multi_layout_selects_layout_and_storage(void **state)
{
	char specifier[] = "layout1/spinor";

	(void)state;
	reset_mock_state();
	assert_loads_as(FLASHMAP_MULTI_JSON_FILENAME, specifier,
			QDL_STORAGE_SPINOR, 2, 2);
	reset_mock_state();
}

static void test_flashmap_multi_layout_can_select_first_layout(void **state)
{
	char specifier[] = "layout0/ufs";

	(void)state;
	reset_mock_state();
	assert_loads_as(FLASHMAP_MULTI_JSON_FILENAME, specifier,
			QDL_STORAGE_UFS, 1, 0);
	reset_mock_state();
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_flashmap_json_loads),
		cmocka_unit_test(test_flashmap_zip_loads),
		cmocka_unit_test(test_flashmap_storage_filter_loads),
		cmocka_unit_test(test_flashmap_multi_layout_requires_selector),
		cmocka_unit_test(test_flashmap_multi_layout_selects_layout),
		cmocka_unit_test(test_flashmap_multi_layout_selects_layout_and_storage),
		cmocka_unit_test(test_flashmap_multi_layout_can_select_first_layout),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
