// SPDX-License-Identifier: BSD-3-Clause
/*
 * Unit tests for the pure helpers in src/util.c: the storage address
 * parser, the storage-type encode/decode pair, and the XML attribute
 * accessors.
 *
 * util.c also contains load_sahara_image(), which references the
 * qdl_file_* and ux_err symbols; the tests below never call it, but the
 * whole translation unit is linked in, so trivial stubs are provided to
 * satisfy the linker.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>
#include <libxml/tree.h>

#include "qdl.h"
#include "file.h"

#ifdef _WIN32
const char *__progname = "test_util";
#endif

/* --- stubs for symbols referenced by unrelated util.c code --- */
void ux_err(const char *fmt, ...) { (void)fmt; }
int qdl_file_open(struct qdl_zip *z, const char *f, struct qdl_file *file)
{ (void)z; (void)f; (void)file; return -1; }
void *qdl_file_load(struct qdl_file *file, size_t *len) { (void)file; (void)len; return NULL; }
void qdl_file_close(struct qdl_file *file) { (void)file; }

/* --- parse_storage_address --- */

static void test_parse_addr_partition_only(void **state)
{
	int part = 99;
	unsigned int start = 99, num = 99;
	char *gpt = (char *)0x1;
	(void)state;

	assert_int_equal(parse_storage_address("3", &part, &start, &num, &gpt), 0);
	assert_int_equal(part, 3);
	assert_int_equal(start, 0);
	assert_int_equal(num, 0);
	assert_null(gpt);
}

static void test_parse_addr_partition_sector(void **state)
{
	int part = 0;
	unsigned int start = 0, num = 99;
	char *gpt = (char *)0x1;
	(void)state;

	assert_int_equal(parse_storage_address("2/100", &part, &start, &num, &gpt), 0);
	assert_int_equal(part, 2);
	assert_int_equal(start, 100);
	assert_int_equal(num, 0);
	assert_null(gpt);
}

static void test_parse_addr_sector_length(void **state)
{
	int part = 0;
	unsigned int start = 0, num = 0;
	char *gpt = (char *)0x1;
	(void)state;

	assert_int_equal(parse_storage_address("1/50+10", &part, &start, &num, &gpt), 0);
	assert_int_equal(part, 1);
	assert_int_equal(start, 50);
	assert_int_equal(num, 10);
	assert_null(gpt);
}

static void test_parse_addr_gpt_name(void **state)
{
	int part = 0;
	unsigned int start = 0, num = 0;
	char *gpt = NULL;
	(void)state;

	/* Bare name: match across all physical partitions. */
	assert_int_equal(parse_storage_address("system", &part, &start, &num, &gpt), 0);
	assert_int_equal(part, -1);
	assert_non_null(gpt);
	assert_string_equal(gpt, "system");
	free(gpt);

	/* N/name: match within a physical partition. */
	gpt = NULL;
	assert_int_equal(parse_storage_address("2/boot", &part, &start, &num, &gpt), 0);
	assert_int_equal(part, 2);
	assert_non_null(gpt);
	assert_string_equal(gpt, "boot");
	free(gpt);
}

static void test_parse_addr_errors(void **state)
{
	int part;
	unsigned int start, num;
	char *gpt;
	(void)state;

	/* Zero length is rejected. */
	assert_int_equal(parse_storage_address("1/50+0", &part, &start, &num, &gpt), -1);
	/* Missing length digits after '+'. */
	assert_int_equal(parse_storage_address("1/50+", &part, &start, &num, &gpt), -1);
	/* Non-numeric length. */
	assert_int_equal(parse_storage_address("1/50+abc", &part, &start, &num, &gpt), -1);
	/* Trailing separator that is neither '+' nor end. */
	assert_int_equal(parse_storage_address("1/2/3", &part, &start, &num, &gpt), -1);
}

/* --- storage type encode/decode --- */

static void test_storage_type_roundtrip(void **state)
{
	const enum qdl_storage_type types[] = {
		QDL_STORAGE_EMMC, QDL_STORAGE_NAND, QDL_STORAGE_UFS,
		QDL_STORAGE_NVME, QDL_STORAGE_SPINOR,
	};
	size_t i;
	(void)state;

	assert_int_equal(decode_storage_type("emmc"), QDL_STORAGE_EMMC);
	assert_int_equal(decode_storage_type("ufs"), QDL_STORAGE_UFS);
	assert_int_equal(decode_storage_type("nvme"), QDL_STORAGE_NVME);
	assert_int_equal(decode_storage_type("bogus"), QDL_STORAGE_UNKNOWN);
	assert_int_equal(decode_storage_type(NULL), QDL_STORAGE_UNKNOWN);

	assert_string_equal(encode_storage_type(QDL_STORAGE_UFS), "ufs");
	assert_null(encode_storage_type((enum qdl_storage_type)999));

	for (i = 0; i < ARRAY_SIZE(types); i++)
		assert_int_equal(decode_storage_type(encode_storage_type(types[i])), types[i]);
}

/* --- attr_as_* --- */

static void test_attr_accessors(void **state)
{
	xmlDoc *doc;
	xmlNode *node;
	int errors = 0;
	const char *s;
	(void)state;

	doc = xmlNewDoc((const xmlChar *)"1.0");
	node = xmlNewNode(NULL, (const xmlChar *)"n");
	xmlNewProp(node, (const xmlChar *)"num", (const xmlChar *)"42");
	xmlNewProp(node, (const xmlChar *)"str", (const xmlChar *)"hello");
	xmlNewProp(node, (const xmlChar *)"empty", (const xmlChar *)"");
	xmlNewProp(node, (const xmlChar *)"flag", (const xmlChar *)"true");
	xmlNewProp(node, (const xmlChar *)"flag0", (const xmlChar *)"false");

	/* Present attributes do not raise errors. */
	assert_int_equal(attr_as_unsigned(node, "num", &errors), 42);
	assert_int_equal(errors, 0);

	s = attr_as_string(node, "str", &errors);
	assert_non_null(s);
	assert_string_equal(s, "hello");
	assert_int_equal(errors, 0);
	free((void *)s);

	assert_true(attr_as_bool(node, "flag", &errors));
	assert_false(attr_as_bool(node, "flag0", &errors));
	assert_int_equal(errors, 0);

	/*
	 * A present-but-empty string attribute returns NULL without flagging
	 * an error - callers must treat NULL as "not provided". This is the
	 * behaviour several load paths depend on.
	 */
	assert_null(attr_as_string(node, "empty", &errors));
	assert_int_equal(errors, 0);

	/* A missing attribute raises an error via the accessor. */
	assert_null(attr_as_string(node, "missing", &errors));
	assert_int_equal(errors, 1);
	errors = 0;
	assert_int_equal(attr_as_unsigned(node, "missing", &errors), 0);
	assert_int_equal(errors, 1);

	/* A missing bool is simply false and raises no error. */
	errors = 0;
	assert_false(attr_as_bool(node, "missing", &errors));
	assert_int_equal(errors, 0);

	xmlFreeNode(node);
	xmlFreeDoc(doc);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_parse_addr_partition_only),
		cmocka_unit_test(test_parse_addr_partition_sector),
		cmocka_unit_test(test_parse_addr_sector_length),
		cmocka_unit_test(test_parse_addr_gpt_name),
		cmocka_unit_test(test_parse_addr_errors),
		cmocka_unit_test(test_storage_type_roundtrip),
		cmocka_unit_test(test_attr_accessors),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
