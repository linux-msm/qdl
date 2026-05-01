// SPDX-License-Identifier: BSD-3-Clause
#define _FILE_OFFSET_BITS 64
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <cmocka.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "file.h"
#include "firehose.h"
#include "list.h"
#include "pathbuf.h"
#include "program.h"
#include "qdl.h"
#include "sparse.h"

#define TEST_INCDIR "/mock/incdir"
#define TEST_XMLDIR "/mock/xml"
#define TEST_PROGRAM_FILE TEST_XMLDIR "/program.xml"
#define TEST_PAYLOAD "payload.bin"
#define TEST_INCDIR_PAYLOAD TEST_INCDIR "/" TEST_PAYLOAD
#define TEST_XMLDIR_PAYLOAD TEST_XMLDIR "/" TEST_PAYLOAD

#ifdef _WIN32
const char *__progname = "test_program_load_xml";
#endif

bool qdl_debug;

static const char *existing_paths[4];

static void set_existing_paths(const char *path0, const char *path1, const char *path2)
{
	existing_paths[0] = path0;
	existing_paths[1] = path1;
	existing_paths[2] = path2;
	existing_paths[3] = NULL;
}

static bool mock_path_exists(const char *path)
{
	for (size_t i = 0; existing_paths[i]; i++) {
		if (!strcmp(path, existing_paths[i]))
			return true;
	}

	return false;
}

int mock_access(const char *path, int mode)
{
	assert_int_equal(mode, F_OK);

	if (mock_path_exists(path))
		return 0;

	errno = ENOENT;
	return -1;
}

int qdl_file_open(struct qdl_zip *qzip, const char *filename, struct qdl_file *file)
{
	assert_null(qzip);

	if (!mock_path_exists(filename))
		return -1;

	file->type = QDL_FILE_TYPE_POSIX;
	file->size = 1;
	file->fd = -1;
	file->zip_file = NULL;
	return 0;
}

void qdl_file_close(struct qdl_file *file)
{
	file->type = QDL_FILE_TYPE_UNKNOWN;
}

void *qdl_file_load(struct qdl_file *file, size_t *len)
{
	(void)file;
	(void)len;
	return NULL;
}

size_t qdl_file_getsize(struct qdl_file *file)
{
	return file->size;
}

off_t qdl_file_seek(struct qdl_file *file, off_t offset, int whence)
{
	(void)file;
	(void)offset;
	(void)whence;
	return -1;
}

ssize_t qdl_file_read(struct qdl_file *file, void *buf, size_t len)
{
	(void)file;
	(void)buf;
	(void)len;
	return -1;
}

struct qdl_zip *qdl_zip_get(struct qdl_zip *qzip)
{
	return qzip;
}

void qdl_zip_put(struct qdl_zip *qzip)
{
	(void)qzip;
}

int sparse_header_parse(struct qdl_file *file, sparse_header_t *sparse_header)
{
	(void)file;
	(void)sparse_header;
	return -1;
}

int sparse_chunk_header_parse(struct qdl_file *file,
			      sparse_header_t *sparse_header,
			      uint64_t *chunk_size,
			      uint32_t *value, off_t *offset)
{
	(void)file;
	(void)sparse_header;
	(void)chunk_size;
	(void)value;
	(void)offset;
	return -1;
}

void ux_init(void)
{
}

void ux_err(const char *fmt, ...)
{
	(void)fmt;
}

void ux_info(const char *fmt, ...)
{
	(void)fmt;
}

void ux_log(const char *fmt, ...)
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

struct firehose_op *firehose_alloc_op(int type)
{
	struct firehose_op *op;

	op = calloc(1, sizeof(*op));
	if (!op)
		return NULL;

	op->type = type;
	return op;
}

void firehose_free_ops(struct list_head *ops)
{
	struct firehose_op *next;
	struct firehose_op *op;

	list_for_each_entry_safe(op, next, ops, node) {
		list_del(&op->node);
		qdl_zip_put(op->zip);
		free((void *)op->filename);
		free((void *)op->label);
		free((void *)op->start_sector);
		free((void *)op->gpt_partition);
		free((void *)op->value);
		free((void *)op->what);
		free(op);
	}
}

int contents_resolve_path(struct contents_filter *filter, const char *filename, struct pathbuf *path)
{
	(void)filter;
	(void)filename;
	(void)path;

	return 0;
}

static xmlDoc *build_program_doc(const char *filename)
{
	char xml[1024];
	int ret;

	ret = snprintf(xml, sizeof(xml),
		       "<data>"
		       "<program "
		       "SECTOR_SIZE_IN_BYTES=\"512\" "
		       "filename=\"%s\" "
		       "label=\"test\" "
		       "num_partition_sectors=\"1\" "
		       "physical_partition_number=\"0\" "
		       "start_sector=\"0\" "
		       "file_sector_offset=\"0\""
		       "/>"
		       "</data>",
		       filename);
	assert_true(ret > 0 && (size_t)ret < sizeof(xml));

	return xmlReadMemory(xml, ret, "program.xml", NULL, 0);
}

static struct firehose_op *get_only_op(struct list_head *ops)
{
	struct firehose_op *op;
	struct list_head *it;
	size_t count = 0;

	list_for_each(it, ops)
		count++;

	if (count != 1)
		fail_msg("expected exactly one firehose op, got %zu", count);

	op = list_entry_first(ops, struct firehose_op, node);
	if (op->type != FIREHOSE_OP_PROGRAM)
		fail_msg("expected firehose op type PROGRAM, got %d", op->type);

	return op;
}

static void test_incdir_takes_precedence(void **state)
{
	struct firehose_op *op;
	struct list_head ops = LIST_INIT(ops);
	xmlDoc *doc;
	int ret;

	(void)state;
	set_existing_paths(TEST_INCDIR_PAYLOAD, TEST_XMLDIR_PAYLOAD, NULL);

	doc = build_program_doc(TEST_PAYLOAD);
	if (!doc)
		fail_msg("failed to parse synthetic program XML");

	ret = program_load_xml(&ops, doc, NULL, TEST_PROGRAM_FILE, false, false, NULL, TEST_INCDIR);
	if (ret)
		fail_msg("incdir payload should be accepted, got %d", ret);

	op = get_only_op(&ops);
	if (!op->filename || strcmp(op->filename, TEST_INCDIR_PAYLOAD))
		fail_msg("incdir should take precedence, got '%s'",
			 op->filename ? op->filename : "(null)");

	firehose_free_ops(&ops);
	xmlFreeDoc(doc);
}

static void test_program_xml_directory_is_used(void **state)
{
	struct firehose_op *op;
	struct list_head ops = LIST_INIT(ops);
	xmlDoc *doc;
	int ret;

	(void)state;
	set_existing_paths(TEST_XMLDIR_PAYLOAD, NULL, NULL);

	doc = build_program_doc(TEST_PAYLOAD);
	if (!doc)
		fail_msg("failed to parse synthetic program XML");

	ret = program_load_xml(&ops, doc, NULL, TEST_PROGRAM_FILE, false, false, NULL, TEST_INCDIR);
	if (ret)
		fail_msg("XML-adjacent payload should be accepted, got %d", ret);

	op = get_only_op(&ops);
	if (!op->filename || strcmp(op->filename, TEST_XMLDIR_PAYLOAD))
		fail_msg("program XML directory should be used, got '%s'",
			 op->filename ? op->filename : "(null)");

	firehose_free_ops(&ops);
	xmlFreeDoc(doc);
}

static void test_program_xml_directory_takes_precedence_over_current_directory(void **state)
{
	struct firehose_op *op;
	struct list_head ops = LIST_INIT(ops);
	xmlDoc *doc;
	int ret;

	(void)state;
	set_existing_paths(TEST_XMLDIR_PAYLOAD, TEST_PAYLOAD, NULL);

	doc = build_program_doc(TEST_PAYLOAD);
	if (!doc)
		fail_msg("failed to parse synthetic program XML");

	ret = program_load_xml(&ops, doc, NULL, TEST_PROGRAM_FILE, false, false, NULL, TEST_INCDIR);
	if (ret)
		fail_msg("XML-adjacent payload should beat current-directory fallback, got %d", ret);

	op = get_only_op(&ops);
	if (!op->filename || strcmp(op->filename, TEST_XMLDIR_PAYLOAD))
		fail_msg("program XML directory should take precedence over current directory, got '%s'",
			 op->filename ? op->filename : "(null)");

	firehose_free_ops(&ops);
	xmlFreeDoc(doc);
}

static void test_current_directory_is_used_when_path_resolution_misses(void **state)
{
	struct firehose_op *op;
	struct list_head ops = LIST_INIT(ops);
	xmlDoc *doc;
	int ret;

	(void)state;
	set_existing_paths(TEST_PAYLOAD, NULL, NULL);

	doc = build_program_doc(TEST_PAYLOAD);
	if (!doc)
		fail_msg("failed to parse synthetic program XML");

	/* Compatibility fallback: leave the original relative filename intact. */
	ret = program_load_xml(&ops, doc, NULL, TEST_PROGRAM_FILE, false, false, NULL, TEST_INCDIR);
	if (ret)
		fail_msg("relative fallback payload should be accepted, got %d", ret);

	op = get_only_op(&ops);
	if (!op->filename || strcmp(op->filename, TEST_PAYLOAD))
		fail_msg("unresolved relative filename should be preserved, got '%s'",
			 op->filename ? op->filename : "(null)");

	firehose_free_ops(&ops);
	xmlFreeDoc(doc);
}

static void test_missing_file_fails_without_allow_missing(void **state)
{
	struct list_head ops = LIST_INIT(ops);
	xmlDoc *doc;
	int ret;

	(void)state;
	set_existing_paths(NULL, NULL, NULL);

	doc = build_program_doc(TEST_PAYLOAD);
	if (!doc)
		fail_msg("failed to parse synthetic program XML");

	ret = program_load_xml(&ops, doc, NULL, TEST_PROGRAM_FILE, false, false, NULL, TEST_INCDIR);
	if (ret != -1)
		fail_msg("missing payload should fail without allow_missing, got %d", ret);
	if (!list_empty(&ops))
		fail_msg("missing payload without allow_missing should not append firehose ops");

	xmlFreeDoc(doc);
}

static void test_missing_file_is_tolerated_with_allow_missing(void **state)
{
	struct firehose_op *op;
	struct list_head ops = LIST_INIT(ops);
	xmlDoc *doc;
	int ret;

	(void)state;
	set_existing_paths(NULL, NULL, NULL);

	doc = build_program_doc(TEST_PAYLOAD);
	if (!doc)
		fail_msg("failed to parse synthetic program XML");

	ret = program_load_xml(&ops, doc, NULL, TEST_PROGRAM_FILE, false, true, NULL, TEST_INCDIR);
	if (ret)
		fail_msg("missing payload should be tolerated with allow_missing, got %d", ret);

	op = get_only_op(&ops);
	if (op->filename)
		fail_msg("allow_missing should clear the missing program filename, got '%s'",
			 op->filename);

	firehose_free_ops(&ops);
	xmlFreeDoc(doc);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_incdir_takes_precedence),
		cmocka_unit_test(test_program_xml_directory_is_used),
		cmocka_unit_test(test_program_xml_directory_takes_precedence_over_current_directory),
		cmocka_unit_test(test_current_directory_is_used_when_path_resolution_misses),
		cmocka_unit_test(test_missing_file_fails_without_allow_missing),
		cmocka_unit_test(test_missing_file_is_tolerated_with_allow_missing),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
