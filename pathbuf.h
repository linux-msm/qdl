/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __PATHBUF_H__
#define __PATHBUF_H__

#include <stdbool.h>
#include <limits.h>
#include <stddef.h>

struct pathbuf {
	char buf[PATH_MAX];
	size_t len;
};

void qdl_pathbuf_reset(struct pathbuf *path);
void qdl_pathbuf_dup(struct pathbuf *dst, const struct pathbuf *orig);
int qdl_pathbuf_push(struct pathbuf *path, const char *component);
const char *qdl_pathbuf_str(const struct pathbuf *path);

void qdl_pathbuf_dirname(struct pathbuf *path);

bool qdl_path_exists(const struct pathbuf *path);

#endif
