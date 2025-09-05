// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 * All rights reserved.
 */
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "raw.h"
#include "qdl.h"
#include "oscompat.h"

static struct raw_op *raw_ops;
static struct raw_op *raw_ops_last;

int raw_op_load(const char *raw_op_file)
{
	xmlDoc *doc;
	struct raw_op *raw_op;

	doc = xmlReadFile(raw_op_file, NULL, 0);

	if (!doc) {
		ux_err("failed to parse raw-type file \"%s\"\n", raw_op_file);
		return -EINVAL;
	}

	raw_op = calloc(1, sizeof(struct raw_op));
	raw_op->doc = doc;

	if (raw_ops) {
		raw_ops_last->next = raw_op;
		raw_ops_last = raw_op;
	} else {
		raw_ops = raw_op;
		raw_ops_last = raw_op;
	}

	return 0;
}

int raw_op_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct raw_op *raw_op))
{
	struct raw_op *raw_op;
	int ret;

	for (raw_op = raw_ops; raw_op; raw_op = raw_op->next) {
		ret = apply(qdl, raw_op);
		if (ret)
			return ret;
	}

	return 0;
}
