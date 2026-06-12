/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __PATCH_H__
#define __PATCH_H__

#include <libxml/parser.h>
#include "list.h"

struct qdl_device;
struct firehose_op;

int patch_load(struct list_head *ops, const char *patch_file);
int patch_load_xml(struct list_head *ops, xmlDoc *doc, const char *patch_file);

#endif
