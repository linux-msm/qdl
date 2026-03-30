/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __READ_H__
#define __READ_H__

#include <stdbool.h>

#include "list.h"

struct qdl_device;
struct firehose_op;

int read_op_load(struct list_head *ops, const char *read_op_file, const char *incdir);
int read_cmd_add(struct list_head *ops, const char *address, const char *filename);

#endif
