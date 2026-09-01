// SPDX-License-Identifier: BSD-3-Clause
/*
 * Unit tests for detect_type(), which classifies a positional qdl
 * argument as a command verb or an input XML file (by inspecting the XML
 * root element). XML cases are written to a temporary file.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

#include "input_type.h"
#include "common.h"

void ux_err(const char *fmt, ...) { (void)fmt; }

/* Write @xml to a temp file and return detect_type() of its path. */
static int detect_xml(const char *xml)
{
	char dir[256];
	char path[512];
	FILE *f;
	int ret;

	assert_int_equal(test_make_temp_dir(dir, sizeof(dir), "qdl-type"), 0);
	snprintf(path, sizeof(path), "%s/input.xml", dir);
	f = fopen(path, "w");
	assert_non_null(f);
	fputs(xml, f);
	fclose(f);

	ret = detect_type(path);

	unlink(path);
	rmdir(dir);
	return ret;
}

static void test_verbs(void **state)
{
	(void)state;

	assert_int_equal(detect_type("read"), QDL_CMD_READ);
	assert_int_equal(detect_type("write"), QDL_CMD_WRITE);
	assert_int_equal(detect_type("erase"), QDL_CMD_ERASE);
	assert_int_equal(detect_type("flash"), QDL_CMD_FLASH);
	assert_int_equal(detect_type("sha256"), QDL_CMD_SHA256);
	assert_int_equal(detect_type("reset"), QDL_CMD_RESET);
}

static void test_not_a_verb_or_file(void **state)
{
	(void)state;

	/* Neither a verb nor an existing file. */
	assert_true(detect_type("definitely-not-a-verb-or-file") < 0);
}

static void test_xml_roots(void **state)
{
	(void)state;

	assert_int_equal(detect_xml("<patches/>"), QDL_FILE_PATCH);
	assert_int_equal(detect_xml("<data><program/></data>"), QDL_FILE_PROGRAM);
	assert_int_equal(detect_xml("<data><read/></data>"), QDL_FILE_READ);
	assert_int_equal(detect_xml("<data><ufs/></data>"), QDL_FILE_UFS);
	assert_int_equal(detect_xml("<contents/>"), QDL_FILE_CONTENTS);
}

/*
 * qdl_is_contents_xml() gates whether the sahara-archive subcommand
 * treats its input as a contents document or an id:file mapping list.
 */
static void test_is_contents_xml(void **state)
{
	char dir[256];
	char path[512];
	FILE *f;
	(void)state;

	assert_int_equal(test_make_temp_dir(dir, sizeof(dir), "qdl-type"), 0);
	snprintf(path, sizeof(path), "%s/input.xml", dir);

	f = fopen(path, "w");
	assert_non_null(f);
	fputs("<?xml version=\"1.0\"?><contents/>", f);
	fclose(f);
	assert_true(qdl_is_contents_xml(path));

	f = fopen(path, "w");
	assert_non_null(f);
	fputs("<?xml version=\"1.0\"?><data/>", f);
	fclose(f);
	assert_false(qdl_is_contents_xml(path));

	unlink(path);
	assert_false(qdl_is_contents_xml(path));
	rmdir(dir);
}

static void test_xml_unknown_and_bad(void **state)
{
	(void)state;

	/* Well-formed XML with an unrecognized root or no known child. */
	assert_int_equal(detect_xml("<something-else/>"), QDL_FILE_UNKNOWN);
	assert_int_equal(detect_xml("<data><whatever/></data>"), QDL_FILE_UNKNOWN);

	/* Not well-formed XML. */
	assert_true(detect_xml("this is not xml <<<") < 0);
}

static void test_verb_precedence_over_data_child(void **state)
{
	(void)state;

	/*
	 * The verb match happens before any file access, so a data document
	 * whose first recognized child is "read" is classified as a file, not
	 * confused with the read verb.
	 */
	assert_int_equal(detect_xml("<data><read/></data>"), QDL_FILE_READ);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_verbs),
		cmocka_unit_test(test_not_a_verb_or_file),
		cmocka_unit_test(test_xml_roots),
		cmocka_unit_test(test_xml_unknown_and_bad),
		cmocka_unit_test(test_is_contents_xml),
		cmocka_unit_test(test_verb_precedence_over_data_child),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
