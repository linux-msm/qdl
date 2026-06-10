/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __CONTENTS_H__
#define __CONTENTS_H__

#include <stdbool.h>
#include <stddef.h>

#include "list.h"

struct sahara_image;
struct pathbuf;
struct contents_filter;

int contents_load(struct list_head *ops, const char *filename, char *specifier,
		  struct sahara_image *images, const char *incdir);
int contents_resolve_path(struct contents_filter *filter, const char *filename, struct pathbuf *path);

#endif
