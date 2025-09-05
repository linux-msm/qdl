/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __RAW_H__
#define __RAW_H__

#include <libxml/parser.h>

struct qdl_device;

struct raw_op {
	xmlDoc *doc;
	struct raw_op *next;
};

int raw_op_load(const char *raw_op_file);
int raw_op_execute(struct qdl_device *qdl,
		    int (*apply)(struct qdl_device *qdl, struct raw_op *raw_op));

#endif
