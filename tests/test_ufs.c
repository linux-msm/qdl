// SPDX-License-Identifier: BSD-3-Clause
/*
 * Unit tests for the UFS provisioning loader. ufs_load() now operates on a
 * caller-owned struct ufs_provisioning instead of module globals, which is
 * what makes it testable here: the tests write a provisioning XML to a temp
 * file, load it into a fresh context, and assert both correct parsing and
 * that every error path leaves the context cleaned up.
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

#include "qdl.h"
#include "file.h"
#include "ufs.h"
#include "common.h"

#ifdef _WIN32
const char *__progname = "test_ufs";
#endif

/* --- stubs --- */
void ux_err(const char *fmt, ...) { (void)fmt; }
void ux_info(const char *fmt, ...) { (void)fmt; }

int qdl_file_open(struct qdl_zip *z, const char *f, struct qdl_file *file)
{ (void)z; (void)f; (void)file; return -1; }
void *qdl_file_load(struct qdl_file *file, size_t *len) { (void)file; (void)len; return NULL; }
void qdl_file_close(struct qdl_file *file) { (void)file; }

/* --- XML fragments --- */
#define UFS_COMMON(lock) \
	"<ufs bNumberLU=\"3\" bBootEnable=\"1\" bDescrAccessEn=\"1\" " \
	"bInitPowerMode=\"1\" bHighPriorityLUN=\"0\" bSecureRemovalType=\"0\" " \
	"bInitActiveICCLevel=\"0\" wPeriodicRTCUpdate=\"0\" bConfigDescrLock=\"" lock "\"/>"

#define UFS_BODY \
	"<ufs LUNum=\"0\" bLUEnable=\"1\" bBootLunID=\"1\" size_in_kb=\"1024\" " \
	"bDataReliability=\"1\" bLUWriteProtect=\"0\" bMemoryType=\"0\" " \
	"bLogicalBlockSize=\"12\" bProvisioningType=\"0\" wContextCapabilities=\"0\" " \
	"desc=\"system\"/>"

#define UFS_EPILOGUE "<ufs commit=\"1\" LUNtoGrow=\"0\"/>"

/* --- helper: write @xml to a temp file and load it into @ufs --- */
static int load_xml(struct ufs_provisioning *ufs, const char *xml, bool finalize)
{
	char dir[256];
	char path[512];
	FILE *f;
	int ret;

	assert_int_equal(test_make_temp_dir(dir, sizeof(dir), "qdl-ufs"), 0);
	snprintf(path, sizeof(path), "%s/ufs.xml", dir);
	f = fopen(path, "w");
	assert_non_null(f);
	fputs(xml, f);
	fclose(f);

	ret = ufs_load(ufs, path, finalize);

	unlink(path);
	rmdir(dir);
	return ret;
}

static void assert_empty(const struct ufs_provisioning *ufs)
{
	assert_false(ufs_need_provisioning(ufs));
	assert_null(ufs->common);
	assert_null(ufs->epilogue);
}

static void test_load_valid(void **state)
{
	struct ufs_provisioning ufs;
	const char *xml =
		"<data>" UFS_COMMON("0") UFS_BODY UFS_EPILOGUE "</data>";
	(void)state;

	ufs_provisioning_init(&ufs);
	assert_int_equal(load_xml(&ufs, xml, false), 0);
	assert_true(ufs_need_provisioning(&ufs));
	assert_non_null(ufs.common);
	assert_int_equal(ufs.common->bNumberLU, 3);
	assert_false(list_empty(&ufs.bodies));
	assert_non_null(ufs.epilogue);

	ufs_provisioning_cleanup(&ufs);
	assert_empty(&ufs);
}

static void test_load_incomplete_cleans_up(void **state)
{
	struct ufs_provisioning ufs;
	/* No epilogue tag -> incomplete. */
	const char *xml = "<data>" UFS_COMMON("0") UFS_BODY "</data>";
	(void)state;

	ufs_provisioning_init(&ufs);
	assert_true(load_xml(&ufs, xml, false) < 0);
	/* On any error the context must be left empty, not dangling. */
	assert_empty(&ufs);

	ufs_provisioning_cleanup(&ufs); /* safe no-op after a failed load */
}

static void test_load_lock_mismatch_cleans_up(void **state)
{
	struct ufs_provisioning ufs;
	/* bConfigDescrLock=1 without --finalize-provisioning is a mismatch. */
	const char *xml =
		"<data>" UFS_COMMON("1") UFS_BODY UFS_EPILOGUE "</data>";
	(void)state;

	ufs_provisioning_init(&ufs);
	assert_true(load_xml(&ufs, xml, false) < 0);
	/*
	 * This path historically freed nothing and left the globals set; the
	 * context must now be fully cleaned up.
	 */
	assert_empty(&ufs);

	ufs_provisioning_cleanup(&ufs);
}

static void test_load_finalize_ok(void **state)
{
	struct ufs_provisioning ufs;
	const char *xml =
		"<data>" UFS_COMMON("1") UFS_BODY UFS_EPILOGUE "</data>";
	(void)state;

	ufs_provisioning_init(&ufs);
	assert_int_equal(load_xml(&ufs, xml, true), 0);
	assert_true(ufs_need_provisioning(&ufs));

	ufs_provisioning_cleanup(&ufs);
	assert_empty(&ufs);
}

static void test_load_duplicate_common_cleans_up(void **state)
{
	struct ufs_provisioning ufs;
	const char *xml =
		"<data>" UFS_COMMON("0") UFS_COMMON("0") UFS_BODY UFS_EPILOGUE "</data>";
	(void)state;

	ufs_provisioning_init(&ufs);
	assert_true(load_xml(&ufs, xml, false) < 0);
	assert_empty(&ufs);

	ufs_provisioning_cleanup(&ufs);
}

static void test_cleanup_idempotent(void **state)
{
	struct ufs_provisioning ufs;
	struct ufs_provisioning snapshot;
	const char *xml =
		"<data>" UFS_COMMON("0") UFS_BODY UFS_EPILOGUE "</data>";
	(void)state;

	ufs_provisioning_init(&ufs);
	assert_int_equal(load_xml(&ufs, xml, false), 0);

	/*
	 * A repeated cleanup must be a bit-exact no-op, not merely leave
	 * the context looking empty: snapshot the context after the first
	 * cleanup and require the second to change nothing.
	 */
	ufs_provisioning_cleanup(&ufs);
	memcpy(&snapshot, &ufs, sizeof(ufs));
	ufs_provisioning_cleanup(&ufs);
	assert_memory_equal(&ufs, &snapshot, sizeof(ufs));
	assert_empty(&ufs);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_load_valid),
		cmocka_unit_test(test_load_incomplete_cleans_up),
		cmocka_unit_test(test_load_lock_mismatch_cleans_up),
		cmocka_unit_test(test_load_finalize_ok),
		cmocka_unit_test(test_load_duplicate_common_cleans_up),
		cmocka_unit_test(test_cleanup_idempotent),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
