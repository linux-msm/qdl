// SPDX-License-Identifier: BSD-3-Clause
#define _FILE_OFFSET_BITS 64

#include <dirent.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmocka.h>

#include "oscompat.h"

#define WORKDIR "test_oscompat_workdir"

static bool is_dir(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void rmtree(const char *path)
{
	struct dirent *ent;
	DIR *dir;

	dir = opendir(path);
	if (dir) {
		while ((ent = readdir(dir)) != NULL) {
			char child[PATH_MAX];
			struct stat st;

			if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
				continue;

			snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
			if (stat(child, &st) == 0 && S_ISDIR(st.st_mode))
				rmtree(child);
			else
				unlink(child);
		}
		closedir(dir);
	}

	rmdir(path);
}

/* ----------------------------- path_is_absolute -------------------------- */

static void test_path_is_absolute(void **state)
{
	(void)state;

#ifndef _WIN32
	assert_true(path_is_absolute("/"));
	assert_true(path_is_absolute("/foo/bar"));
	assert_false(path_is_absolute("foo"));
	assert_false(path_is_absolute("./foo"));
	assert_false(path_is_absolute(""));
#else
	assert_true(path_is_absolute("C:\\foo"));
	assert_true(path_is_absolute("C:/foo"));
	assert_true(path_is_absolute("c:foo"));		/* drive-relative */
	assert_true(path_is_absolute("\\\\server\\share"));
	assert_false(path_is_absolute("foo"));
	assert_false(path_is_absolute("\\foo"));	/* single backslash */
	assert_false(path_is_absolute(""));
#endif
}

/* ------------------------------- qdl_mkdir_p ----------------------------- */

static void test_mkdir_p_creates_nested(void **state)
{
	(void)state;

	assert_int_equal(qdl_mkdir_p(WORKDIR "/a/b/c"), 0);
	assert_true(is_dir(WORKDIR));
	assert_true(is_dir(WORKDIR "/a"));
	assert_true(is_dir(WORKDIR "/a/b"));
	assert_true(is_dir(WORKDIR "/a/b/c"));
}

static void test_mkdir_p_is_idempotent(void **state)
{
	(void)state;

	assert_int_equal(qdl_mkdir_p(WORKDIR "/idem"), 0);
	/* An already existing directory is not an error. */
	assert_int_equal(qdl_mkdir_p(WORKDIR "/idem"), 0);
	assert_true(is_dir(WORKDIR "/idem"));
}

static void test_mkdir_p_trailing_slash(void **state)
{
	(void)state;

	assert_int_equal(qdl_mkdir_p(WORKDIR "/x/y/"), 0);
	assert_true(is_dir(WORKDIR "/x/y"));
}

static void test_mkdir_p_existing_and_dot(void **state)
{
	(void)state;

	/* The current directory always exists; this must succeed. */
	assert_int_equal(qdl_mkdir_p("."), 0);
	/* An empty path is a no-op. */
	assert_int_equal(qdl_mkdir_p(""), 0);
}

static void test_mkdir_p_too_long(void **state)
{
	char path[PATH_MAX + 16];

	(void)state;

	memset(path, 'a', sizeof(path) - 1);
	path[sizeof(path) - 1] = '\0';

	assert_int_equal(qdl_mkdir_p(path), -1);
	assert_int_equal(errno, ENAMETOOLONG);
}

static void test_mkdir_p_component_not_a_dir(void **state)
{
	const char *file = WORKDIR "/afile";
	FILE *fp;

	(void)state;

	assert_int_equal(qdl_mkdir_p(WORKDIR), 0);
	fp = fopen(file, "wb");
	assert_non_null(fp);
	fclose(fp);

	/* A path component that is a regular file cannot be descended into. */
	assert_int_equal(qdl_mkdir_p(WORKDIR "/afile/sub"), -1);
}

static int group_setup(void **state)
{
	(void)state;
	rmtree(WORKDIR);
	return 0;
}

static int group_teardown(void **state)
{
	(void)state;
	rmtree(WORKDIR);
	return 0;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_path_is_absolute),
		cmocka_unit_test(test_mkdir_p_creates_nested),
		cmocka_unit_test(test_mkdir_p_is_idempotent),
		cmocka_unit_test(test_mkdir_p_trailing_slash),
		cmocka_unit_test(test_mkdir_p_existing_and_dot),
		cmocka_unit_test(test_mkdir_p_too_long),
		cmocka_unit_test(test_mkdir_p_component_not_a_dir),
	};

	return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
