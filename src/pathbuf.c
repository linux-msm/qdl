// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <errno.h>
#include <sys/stat.h>
#include <string.h>

#include "oscompat.h"
#include "pathbuf.h"

static bool qdl_path_is_sep(char ch)
{
#ifdef _WIN32
	return ch == '/' || ch == '\\';
#else
	return ch == '/';
#endif
}

static char *qdl_pathbuf_last_sep(char *path)
{
	char *last = NULL;

	for (; *path; path++) {
		if (qdl_path_is_sep(*path))
			last = path;
	}

	return last;
}

#ifdef _WIN32
static size_t qdl_pathbuf_unc_root_len(const char *path)
{
	size_t i;
	size_t start;

	if (!qdl_path_is_sep(path[0]) || !qdl_path_is_sep(path[1]))
		return 0;

	i = 2;
	while (qdl_path_is_sep(path[i]))
		i++;
	if (!path[i])
		return 2;

	start = i;
	while (path[i] && !qdl_path_is_sep(path[i]))
		i++;
	if (i == start)
		return 2;

	while (qdl_path_is_sep(path[i]))
		i++;
	if (!path[i])
		return 2;

	start = i;
	while (path[i] && !qdl_path_is_sep(path[i]))
		i++;
	if (i == start)
		return 2;

	return i;
}
#endif

void qdl_pathbuf_reset(struct pathbuf *path)
{
	if (!path)
		return;

	path->buf[0] = '\0';
	path->len = 0;
}

void qdl_pathbuf_dup(struct pathbuf *dst, const struct pathbuf *orig)
{
	memcpy(dst, orig, sizeof(struct pathbuf));
}

int qdl_pathbuf_push(struct pathbuf *path, const char *component)
{
	size_t component_len;
	size_t path_len;
	size_t skip = 0;
	size_t need_sep = 0;

	if (!path || !component)
		return -EINVAL;

	if (component[0] == '\0')
		return 0;

	path_len = path->len;
	if (path_is_absolute(component))
		path_len = 0;

	if (component[0] == '.' && qdl_path_is_sep(component[1]))
		skip += 2;

	if (path_len > 0)
		need_sep = !qdl_path_is_sep(path->buf[path_len - 1]);

	if (path_len > 0 && !need_sep) {
		while (qdl_path_is_sep(component[skip]))
			skip++;
	}

	component_len = strlen(component + skip);
	if (component_len == 0)
		return 0;

	if (path_len + need_sep + component_len + 1 > sizeof(path->buf))
		return -ENAMETOOLONG;

	if (need_sep)
		path->buf[path_len++] = '/';

#ifndef _WIN32
	memcpy(path->buf + path_len, component + skip, component_len + 1);
#else
	for (size_t i = 0; i <= component_len; i++)
		path->buf[path_len + i] = qdl_path_is_sep(component[skip + i]) ? '/' : component[skip + i];
#endif
	path->len = path_len + component_len;

	return 0;
}

const char *qdl_pathbuf_str(const struct pathbuf *path)
{
	return path ? path->buf : NULL;
}

void qdl_pathbuf_dirname(struct pathbuf *path)
{
	size_t root_len = 0;
	char *sep;

	if (!path || !path->buf[0])
		return;

	if (path->len == 0)
		path->len = strlen(path->buf);

#ifdef _WIN32
	if (isalpha((unsigned char)path->buf[0]) && path->buf[1] == ':' && qdl_path_is_sep(path->buf[2])) {
		root_len = 3;
	} else {
		root_len = qdl_pathbuf_unc_root_len(path->buf);
		if (root_len == 0 && qdl_path_is_sep(path->buf[0]))
			root_len = 1;
	}
#else
	if (path->buf[0] == '/')
		root_len = 1;
#endif

	while (path->len > root_len && qdl_path_is_sep(path->buf[path->len - 1])) {
		path->buf[--path->len] = '\0';
	}

	sep = qdl_pathbuf_last_sep(path->buf);
	if (!sep)
		return;

	if ((size_t)(sep - path->buf) < root_len) {
		path->buf[root_len] = '\0';
		path->len = root_len;
		return;
	}

	*sep = '\0';
	path->len = (size_t)(sep - path->buf);
}

bool qdl_path_exists(const struct pathbuf *path)
{
	struct stat st;

	if (!path || !path->buf[0])
		return false;

	return stat(path->buf, &st) == 0;
}
