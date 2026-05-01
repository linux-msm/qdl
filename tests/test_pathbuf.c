// SPDX-License-Identifier: BSD-3-Clause
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "pathbuf.h"

static void init_path(struct pathbuf *path, const char *s, bool set_len)
{
	size_t len = strlen(s);

	assert_true(len < sizeof(path->buf));
	memcpy(path->buf, s, len + 1);
	path->len = set_len ? len : 0;
}

static void assert_dirname(const char *in, const char *out, bool set_len)
{
	struct pathbuf path;

	init_path(&path, in, set_len);
	qdl_pathbuf_dirname(&path);

	assert_string_equal(path.buf, out);
	assert_int_equal(path.len, strlen(out));
}

static void test_dirname_posix_root(void **state)
{
	(void)state;
	assert_dirname("/", "/", true);
	assert_dirname("/", "/", false);
}

static void test_dirname_posix_absolute(void **state)
{
	(void)state;
	assert_dirname("/foo", "/", true);
	assert_dirname("/foo/bar", "/foo", true);
	assert_dirname("/foo/bar/", "/foo", true);
}

static void test_dirname_relative(void **state)
{
	(void)state;
	assert_dirname("foo", "foo", true);
	assert_dirname("foo/bar", "foo", true);
	assert_dirname("foo/bar/", "foo", true);
}

static void test_reset_and_dup(void **state)
{
	struct pathbuf src;
	struct pathbuf dst;

	(void)state;

	init_path(&src, "/tmp/example", true);
	qdl_pathbuf_dup(&dst, &src);
	assert_string_equal(dst.buf, src.buf);
	assert_int_equal(dst.len, src.len);

	qdl_pathbuf_reset(&dst);
	assert_string_equal(dst.buf, "");
	assert_int_equal(dst.len, 0);

	qdl_pathbuf_reset(NULL);
}

static void test_push_basic_behavior(void **state)
{
	struct pathbuf path = {0};
	int ret;

	(void)state;

	ret = qdl_pathbuf_push(&path, "foo");
	assert_int_equal(ret, 0);
	assert_string_equal(path.buf, "foo");
	assert_int_equal(path.len, strlen("foo"));

	ret = qdl_pathbuf_push(&path, "bar");
	assert_int_equal(ret, 0);
	assert_string_equal(path.buf, "foo/bar");
	assert_int_equal(path.len, strlen("foo/bar"));

	ret = qdl_pathbuf_push(&path, "");
	assert_int_equal(ret, 0);
	assert_string_equal(path.buf, "foo/bar");
	assert_int_equal(path.len, strlen("foo/bar"));

	ret = qdl_pathbuf_push(&path, "./baz");
	assert_int_equal(ret, 0);
	assert_string_equal(path.buf, "foo/bar/baz");
	assert_int_equal(path.len, strlen("foo/bar/baz"));

#ifdef _WIN32
	ret = qdl_pathbuf_push(&path, "C:/abs");
	assert_int_equal(ret, 0);
	assert_string_equal(path.buf, "C:/abs");
	assert_int_equal(path.len, strlen("C:/abs"));
#else
	ret = qdl_pathbuf_push(&path, "/abs");
	assert_int_equal(ret, 0);
	assert_string_equal(path.buf, "/abs");
	assert_int_equal(path.len, strlen("/abs"));
#endif
}

static void test_push_invalid_args(void **state)
{
	struct pathbuf path = {0};

	(void)state;

	assert_int_equal(qdl_pathbuf_push(NULL, "foo"), -EINVAL);
	assert_int_equal(qdl_pathbuf_push(&path, NULL), -EINVAL);
}

static void test_push_overflow_keeps_state(void **state)
{
	struct pathbuf path = {0};
	char before[PATH_MAX];
	size_t max_prefix_len;
	int ret;

	(void)state;

	max_prefix_len = sizeof(path.buf) - 1;
	memset(path.buf, 'a', max_prefix_len);
	path.buf[max_prefix_len] = '\0';
	path.len = max_prefix_len;
	memcpy(before, path.buf, sizeof(before));

	ret = qdl_pathbuf_push(&path, "b");
	assert_int_equal(ret, -ENAMETOOLONG);
	assert_string_equal(path.buf, before);
	assert_int_equal(path.len, max_prefix_len);
}

#ifdef _WIN32
static void test_dirname_windows_drive(void **state)
{
	(void)state;
	assert_dirname("C:\\", "C:\\", true);
	assert_dirname("C:\\foo", "C:\\", true);
	assert_dirname("C:\\foo\\bar", "C:\\foo", true);
	assert_dirname("C:/foo/bar", "C:/foo", true);
}

static void test_dirname_windows_unc(void **state)
{
	(void)state;
	assert_dirname("\\\\server\\share", "\\\\server\\share", true);
	assert_dirname("\\\\server\\share\\dir", "\\\\server\\share", true);
	assert_dirname("\\\\server\\share\\dir\\sub", "\\\\server\\share\\dir", true);
}
#endif

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_dirname_posix_root),
		cmocka_unit_test(test_dirname_posix_absolute),
		cmocka_unit_test(test_dirname_relative),
		cmocka_unit_test(test_reset_and_dup),
		cmocka_unit_test(test_push_basic_behavior),
		cmocka_unit_test(test_push_invalid_args),
		cmocka_unit_test(test_push_overflow_keeps_state),
#ifdef _WIN32
		cmocka_unit_test(test_dirname_windows_drive),
		cmocka_unit_test(test_dirname_windows_unc),
#endif
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
