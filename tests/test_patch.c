// SPDX-License-Identifier: BSD-3-Clause
/*
 * Unit tests for lib/patch.c
 *
 * Verifies patch loading, execution, context lifecycle, and error
 * handling without requiring a real device or USB connection.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qdl.h"
#include "patch.h"

/* Globals required by the library */
bool qdl_debug;
const char *__progname = "test_patch";

/* No-op ux backend -- silences all library output during tests */
static void noop_msg(const char *fmt __attribute__((unused)),
		     va_list ap __attribute__((unused)))
{}
static void noop_progress(const char *fmt __attribute__((unused)),
			  unsigned int value __attribute__((unused)),
			  unsigned int max __attribute__((unused)),
			  va_list ap __attribute__((unused)))
{}

static const struct qdl_ux_ops noop_ux = {
	.err      = noop_msg,
	.info     = noop_msg,
	.log      = noop_msg,
	.debug    = noop_msg,
	.progress = noop_progress,
};

/* Write a string to a temporary file, return its path (caller frees) */
static char *write_tmpfile(const char *content)
{
	char *path = strdup(tmpnam(NULL));
	FILE *fp;

	assert_non_null(path);
	fp = fopen(path, "w");
	assert_non_null(fp);
	assert_int_equal(fprintf(fp, "%s", content), (int)strlen(content));
	fclose(fp);
	return path;
}

static unsigned int count_patches(struct qdl_device *qdl)
{
	struct patch *p;
	unsigned int n = 0;

	list_for_each_entry(p, &qdl->patch_ctx.patches, node)
		n++;
	return n;
}

/* --- Tests --- */

static void test_ctx_init(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	patch_init(&qdl);
	assert_false(qdl.patch_ctx.loaded);
	assert_true(list_empty(&qdl.patch_ctx.patches));
	patch_free(&qdl);
}

static void test_load_single_patch(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };
	struct patch *p;

	const char *xml =
		"<?xml version=\"1.0\"?>\n"
		"<patches>\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"4\" "
		"filename=\"DISK\" "
		"physical_partition_number=\"0\" "
		"size_in_bytes=\"4\" "
		"start_sector=\"2\" "
		"value=\"42\" "
		"what=\"Set MBR signature\" />\n"
		"</patches>\n";

	char *path = write_tmpfile(xml);

	patch_init(&qdl);
	assert_int_equal(0, patch_load(&qdl, path));
	assert_true(qdl.patch_ctx.loaded);
	assert_int_equal(count_patches(&qdl), 1);

	p = list_entry_first(&qdl.patch_ctx.patches, struct patch, node);
	assert_int_equal(p->sector_size, 512);
	assert_int_equal(p->byte_offset, 4);
	assert_string_equal(p->filename, "DISK");
	assert_int_equal(p->partition, 0);
	assert_int_equal(p->size_in_bytes, 4);
	assert_string_equal(p->start_sector, "2");
	assert_string_equal(p->value, "42");
	assert_string_equal(p->what, "Set MBR signature");

	patch_free(&qdl);
	unlink(path);
	free(path);
}

static void test_load_multiple_patches(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	const char *xml =
		"<?xml version=\"1.0\"?>\n"
		"<patches>\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"0\" filename=\"DISK\" "
		"physical_partition_number=\"0\" size_in_bytes=\"4\" "
		"start_sector=\"0\" value=\"AA\" what=\"first\" />\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"4096\" "
		"byte_offset=\"8\" filename=\"DISK\" "
		"physical_partition_number=\"1\" size_in_bytes=\"8\" "
		"start_sector=\"10\" value=\"BB\" what=\"second\" />\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"16\" filename=\"DISK\" "
		"physical_partition_number=\"2\" size_in_bytes=\"2\" "
		"start_sector=\"20\" value=\"CC\" what=\"third\" />\n"
		"</patches>\n";

	char *path = write_tmpfile(xml);

	patch_init(&qdl);
	assert_int_equal(0, patch_load(&qdl, path));
	assert_int_equal(count_patches(&qdl), 3);

	patch_free(&qdl);
	unlink(path);
	free(path);
}

static void test_load_missing_file(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	patch_init(&qdl);
	assert_true(patch_load(&qdl, "qdl_test_nonexistent_file.xml") < 0);
	assert_false(qdl.patch_ctx.loaded);
	assert_true(list_empty(&qdl.patch_ctx.patches));

	patch_free(&qdl);
}

static void test_load_malformed_xml(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };
	char *path = write_tmpfile("this is not xml at all");

	patch_init(&qdl);
	assert_true(patch_load(&qdl, path) < 0);
	assert_false(qdl.patch_ctx.loaded);

	patch_free(&qdl);
	unlink(path);
	free(path);
}

static void test_load_missing_attributes(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	/* Missing value and what attributes */
	const char *xml =
		"<?xml version=\"1.0\"?>\n"
		"<patches>\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"0\" filename=\"DISK\" "
		"physical_partition_number=\"0\" size_in_bytes=\"4\" "
		"start_sector=\"0\" />\n"
		"</patches>\n";

	char *path = write_tmpfile(xml);

	patch_init(&qdl);
	assert_int_equal(0, patch_load(&qdl, path));
	/* Patch with missing attrs should be skipped */
	assert_int_equal(count_patches(&qdl), 0);

	patch_free(&qdl);
	unlink(path);
	free(path);
}

static void test_load_ignores_non_patch_tags(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	const char *xml =
		"<?xml version=\"1.0\"?>\n"
		"<patches>\n"
		"  <something_else foo=\"bar\" />\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"0\" filename=\"DISK\" "
		"physical_partition_number=\"0\" size_in_bytes=\"4\" "
		"start_sector=\"0\" value=\"FF\" what=\"test\" />\n"
		"</patches>\n";

	char *path = write_tmpfile(xml);

	patch_init(&qdl);
	assert_int_equal(0, patch_load(&qdl, path));
	assert_int_equal(count_patches(&qdl), 1);

	patch_free(&qdl);
	unlink(path);
	free(path);
}

static void test_load_empty_patches(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	const char *xml =
		"<?xml version=\"1.0\"?>\n"
		"<patches>\n"
		"</patches>\n";

	char *path = write_tmpfile(xml);

	patch_init(&qdl);
	assert_int_equal(0, patch_load(&qdl, path));
	assert_true(qdl.patch_ctx.loaded);
	assert_int_equal(count_patches(&qdl), 0);

	patch_free(&qdl);
	unlink(path);
	free(path);
}

static void test_ctx_free_clears_state(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	const char *xml =
		"<?xml version=\"1.0\"?>\n"
		"<patches>\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"0\" filename=\"DISK\" "
		"physical_partition_number=\"0\" size_in_bytes=\"4\" "
		"start_sector=\"0\" value=\"FF\" what=\"test\" />\n"
		"</patches>\n";

	char *path = write_tmpfile(xml);

	patch_init(&qdl);
	patch_load(&qdl, path);
	assert_int_equal(count_patches(&qdl), 1);
	assert_true(qdl.patch_ctx.loaded);

	patch_free(&qdl);
	assert_true(list_empty(&qdl.patch_ctx.patches));
	assert_false(qdl.patch_ctx.loaded);

	unlink(path);
	free(path);
}

/* Mock apply callback: records what it was called with */
static int apply_call_count;
static unsigned int apply_partitions[16];

static int mock_apply(struct qdl_device *qdl __attribute__((unused)),
		      struct patch *patch)
{
	if (apply_call_count < 16)
		apply_partitions[apply_call_count] = patch->partition;
	apply_call_count++;
	return 0;
}

static void test_execute_calls_apply_for_disk_patches(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	const char *xml =
		"<?xml version=\"1.0\"?>\n"
		"<patches>\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"0\" filename=\"DISK\" "
		"physical_partition_number=\"0\" size_in_bytes=\"4\" "
		"start_sector=\"0\" value=\"AA\" what=\"first\" />\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"4\" filename=\"DISK\" "
		"physical_partition_number=\"1\" size_in_bytes=\"4\" "
		"start_sector=\"2\" value=\"BB\" what=\"second\" />\n"
		"</patches>\n";

	char *path = write_tmpfile(xml);

	patch_init(&qdl);
	patch_load(&qdl, path);

	apply_call_count = 0;
	memset(apply_partitions, 0, sizeof(apply_partitions));

	assert_int_equal(0, patch_execute(&qdl, mock_apply));
	assert_int_equal(apply_call_count, 2);
	assert_int_equal(apply_partitions[0], 0);
	assert_int_equal(apply_partitions[1], 1);

	patch_free(&qdl);
	unlink(path);
	free(path);
}

static void test_execute_skips_non_disk_patches(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	const char *xml =
		"<?xml version=\"1.0\"?>\n"
		"<patches>\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"0\" filename=\"DISK\" "
		"physical_partition_number=\"0\" size_in_bytes=\"4\" "
		"start_sector=\"0\" value=\"AA\" what=\"disk\" />\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"0\" filename=\"other.img\" "
		"physical_partition_number=\"1\" size_in_bytes=\"4\" "
		"start_sector=\"0\" value=\"BB\" what=\"non-disk\" />\n"
		"</patches>\n";

	char *path = write_tmpfile(xml);

	patch_init(&qdl);
	patch_load(&qdl, path);

	apply_call_count = 0;

	assert_int_equal(0, patch_execute(&qdl, mock_apply));
	assert_int_equal(apply_call_count, 1);
	assert_int_equal(apply_partitions[0], 0);

	patch_free(&qdl);
	unlink(path);
	free(path);
}

static int mock_apply_fail(struct qdl_device *qdl __attribute__((unused)),
			   struct patch *patch __attribute__((unused)))
{
	return -1;
}

static void test_execute_stops_on_apply_error(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	const char *xml =
		"<?xml version=\"1.0\"?>\n"
		"<patches>\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"0\" filename=\"DISK\" "
		"physical_partition_number=\"0\" size_in_bytes=\"4\" "
		"start_sector=\"0\" value=\"AA\" what=\"first\" />\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"4\" filename=\"DISK\" "
		"physical_partition_number=\"1\" size_in_bytes=\"4\" "
		"start_sector=\"2\" value=\"BB\" what=\"second\" />\n"
		"</patches>\n";

	char *path = write_tmpfile(xml);

	patch_init(&qdl);
	patch_load(&qdl, path);

	assert_true(patch_execute(&qdl, mock_apply_fail) != 0);

	patch_free(&qdl);
	unlink(path);
	free(path);
}

static void test_execute_noop_when_not_loaded(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	patch_init(&qdl);
	/* No load -- execute should be a no-op returning 0 */
	assert_int_equal(0, patch_execute(&qdl, mock_apply_fail));
	patch_free(&qdl);
}

static void test_ctx_reuse_after_free(void **state)
{
	(void)state;
	struct qdl_device qdl = { 0 };

	const char *xml =
		"<?xml version=\"1.0\"?>\n"
		"<patches>\n"
		"  <patch SECTOR_SIZE_IN_BYTES=\"512\" "
		"byte_offset=\"0\" filename=\"DISK\" "
		"physical_partition_number=\"0\" size_in_bytes=\"4\" "
		"start_sector=\"0\" value=\"FF\" what=\"test\" />\n"
		"</patches>\n";

	char *path = write_tmpfile(xml);

	/* First use */
	patch_init(&qdl);
	patch_load(&qdl, path);
	assert_int_equal(count_patches(&qdl), 1);
	patch_free(&qdl);

	/* Reuse after free */
	patch_init(&qdl);
	assert_true(list_empty(&qdl.patch_ctx.patches));
	assert_false(qdl.patch_ctx.loaded);
	patch_load(&qdl, path);
	assert_int_equal(count_patches(&qdl), 1);
	patch_free(&qdl);

	unlink(path);
	free(path);
}

static int setup(void **state)
{
	(void)state;
	qdl_ux_set_ops(&noop_ux);
	return 0;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		/* Context lifecycle */
		cmocka_unit_test(test_ctx_init),
		cmocka_unit_test(test_ctx_free_clears_state),
		cmocka_unit_test(test_ctx_reuse_after_free),

		/* Loading */
		cmocka_unit_test(test_load_single_patch),
		cmocka_unit_test(test_load_multiple_patches),
		cmocka_unit_test(test_load_empty_patches),
		cmocka_unit_test(test_load_missing_file),
		cmocka_unit_test(test_load_malformed_xml),
		cmocka_unit_test(test_load_missing_attributes),
		cmocka_unit_test(test_load_ignores_non_patch_tags),

		/* Execution */
		cmocka_unit_test(test_execute_calls_apply_for_disk_patches),
		cmocka_unit_test(test_execute_skips_non_disk_patches),
		cmocka_unit_test(test_execute_stops_on_apply_error),
		cmocka_unit_test(test_execute_noop_when_not_loaded),
	};

	return cmocka_run_group_tests(tests, setup, NULL);
}
