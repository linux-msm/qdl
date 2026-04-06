/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __FLASHMAP_H__
#define __FLASHMAP_H__

#include <stdbool.h>
#include "list.h"

struct sahara_image;

int flashmap_load(struct list_head *ops, const char *filename, struct sahara_image *images, const char *incdir);

#endif
