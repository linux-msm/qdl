// SPDX-License-Identifier: BSD-3-Clause
/*
 * Unit tests for the <patch> and <read> XML loaders (patch.c, read.c).
 * They turn XML into firehose op lists; util.c is linked in for the real
 * attribute accessors, and the device/ux layers are stubbed.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>
#include <libxml/parser.h>

#include "list.h"
#include "file.h"
#include "firehose.h"
#include "patch.h"
#include "read.h"
#include "qdl.h"
#include "common.h"

#ifdef _WIN32
const char *__progname = "test_op_loaders";
#endif

/* --- stubs --- */
void ux_init(void) {}
void ux_err(const char *fmt, ...) { (void)fmt; }
void ux_info(const char *fmt, ...) { (void)fmt; }
void ux_log(const char *fmt, ...) { (void)fmt; }
void ux_debug(const char *fmt, ...) { (void)fmt; }
void ux_progress(const char *fmt, unsigned int value, unsigned int size, ...)
{ (void)fmt; (void)value; (void)size; }

/* util.c references these via load_sahara_image(), never called here. */
int qdl_file_open(struct qdl_zip *z, const char *f, struct qdl_file *file)
{ (void)z; (void)f; (void)file; return -1; }
void *qdl_file_load(struct qdl_file *file, size_t *len) { (void)file; (void)len; return NULL; }
void qdl_file_close(struct qdl_file *file) { (void)file; }

struct firehose_op *firehose_alloc_op(int type)
{
	struct firehose_op *op = calloc(1, sizeof(*op));

	if (op)
		op->type = type;
	return op;
}

/* --- helpers --- */
static void free_ops(struct list_head *ops)
{
	struct firehose_op *op;
	struct firehose_op *next;

	list_for_each_entry_safe(op, next, ops, node) {
		list_del(&op->node);
		free((void *)op->filename);
		free((void *)op->label);
		free((void *)op->start_sector);
		free((void *)op->value);
		free((void *)op->what);
		free(op);
	}
}

static unsigned int count_ops(struct list_head *ops)
{
	struct firehose_op *op;
	unsigned int n = 0;

	list_for_each_entry(op, ops, node)
		n++;
	return n;
}

static struct firehose_op *first_op(struct list_head *ops)
{
	return list_entry_first(ops, struct firehose_op, node);
}

/* --- patch loader --- */
static int load_patch_str(struct list_head *ops, const char *xml)
{
	xmlDoc *doc = xmlReadMemory(xml, (int)strlen(xml), "test.xml", NULL, 0);
	int ret;

	assert_non_null(doc);
	ret = patch_load_xml(ops, doc, "test.xml");
	xmlFreeDoc(doc);
	return ret;
}

#define PATCH_OK \
	"<patches><patch SECTOR_SIZE_IN_BYTES=\"512\" byte_offset=\"0\" " \
	"filename=\"DISK\" physical_partition_number=\"1\" size_in_bytes=\"4\" " \
	"start_sector=\"2\" value=\"7\" what=\"crc\"/></patches>"

static void test_patch_valid(void **state)
{
	struct list_head ops = LIST_INIT(ops);
	struct firehose_op *op;
	(void)state;

	assert_int_equal(load_patch_str(&ops, PATCH_OK), 0);
	assert_int_equal(count_ops(&ops), 1);
	op = first_op(&ops);
	assert_int_equal(op->type, FIREHOSE_OP_PATCH);
	assert_int_equal(op->partition, 1);
	assert_int_equal(op->size_in_bytes, 4);
	assert_string_equal(op->filename, "DISK");
	assert_string_equal(op->what, "crc");
	free_ops(&ops);
}

static void test_patch_missing_attr_fails(void **state)
{
	struct list_head ops = LIST_INIT(ops);
	/* Same as PATCH_OK but with the "value" attribute removed. */
	const char *xml =
		"<patches><patch SECTOR_SIZE_IN_BYTES=\"512\" byte_offset=\"0\" "
		"filename=\"DISK\" physical_partition_number=\"1\" size_in_bytes=\"4\" "
		"start_sector=\"2\" what=\"crc\"/></patches>";
	(void)state;

	assert_true(load_patch_str(&ops, xml) < 0);
	free_ops(&ops);
}

static void test_patch_unknown_tag_skipped(void **state)
{
	struct list_head ops = LIST_INIT(ops);
	const char *xml =
		"<patches><bogus/><patch SECTOR_SIZE_IN_BYTES=\"512\" byte_offset=\"0\" "
		"filename=\"DISK\" physical_partition_number=\"1\" size_in_bytes=\"4\" "
		"start_sector=\"2\" value=\"7\" what=\"crc\"/></patches>";
	(void)state;

	/* An unrecognized child is skipped, the valid patch still loads. */
	assert_int_equal(load_patch_str(&ops, xml), 0);
	assert_int_equal(count_ops(&ops), 1);
	free_ops(&ops);
}

/* --- read loader (file based) --- */
static int load_read_file(struct list_head *ops, const char *xml)
{
	char dir[256];
	char path[512];
	FILE *f;
	int ret;

	assert_int_equal(test_make_temp_dir(dir, sizeof(dir), "qdl-read"), 0);
	snprintf(path, sizeof(path), "%s/read.xml", dir);
	f = fopen(path, "w");
	assert_non_null(f);
	fputs(xml, f);
	fclose(f);

	ret = read_op_load(ops, path, NULL);

	unlink(path);
	rmdir(dir);
	return ret;
}

static void test_read_valid(void **state)
{
	struct list_head ops = LIST_INIT(ops);
	struct firehose_op *op;
	const char *xml =
		"<data><read SECTOR_SIZE_IN_BYTES=\"512\" filename=\"out.img\" "
		"physical_partition_number=\"0\" num_partition_sectors=\"10\" "
		"start_sector=\"3\"/></data>";
	(void)state;

	assert_int_equal(load_read_file(&ops, xml), 0);
	assert_int_equal(count_ops(&ops), 1);
	op = first_op(&ops);
	assert_int_equal(op->type, FIREHOSE_OP_READ);
	assert_int_equal(op->num_sectors, 10);
	assert_string_equal(op->filename, "out.img");
	free_ops(&ops);
}

static void test_read_missing_attr_fails(void **state)
{
	struct list_head ops = LIST_INIT(ops);
	/* filename attribute removed. */
	const char *xml =
		"<data><read SECTOR_SIZE_IN_BYTES=\"512\" "
		"physical_partition_number=\"0\" num_partition_sectors=\"10\" "
		"start_sector=\"3\"/></data>";
	(void)state;

	assert_true(load_read_file(&ops, xml) < 0);
	free_ops(&ops);
}

static void test_read_missing_file_fails(void **state)
{
	struct list_head ops = LIST_INIT(ops);
	(void)state;

	assert_true(read_op_load(&ops, "/nonexistent/qdl/read.xml", NULL) < 0);
	free_ops(&ops);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_patch_valid),
		cmocka_unit_test(test_patch_missing_attr_fails),
		cmocka_unit_test(test_patch_unknown_tag_skipped),
		cmocka_unit_test(test_read_valid),
		cmocka_unit_test(test_read_missing_attr_fails),
		cmocka_unit_test(test_read_missing_file_fails),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
