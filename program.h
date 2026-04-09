/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __PROGRAM_H__
#define __PROGRAM_H__

#include <sys/types.h>
#include <libxml/parser.h>
#include <stdbool.h>
#include <stdint.h>
#include <zip.h>
#include "list.h"

struct qdl_device;
struct firehose_op;
struct qdl_zip;

int program_load(struct list_head *ops, const char *program_file, bool is_nand,
		 bool allow_missing, const char *incdir);
int program_load_xml(struct list_head *ops, xmlDoc *doc, struct qdl_zip *zip, const char *program_file,
		     bool is_nand, bool allow_missing, const char *incdir);
int erase_execute(struct qdl_device *qdl, struct firehose_op *op,
		  int (*apply)(struct qdl_device *qdl, struct firehose_op *op));
int program_find_bootable_partition(struct list_head *ops, bool *multiple_found);
int program_is_sec_partition_flashed(struct list_head *ops);
int program_cmd_add(struct list_head *ops, const char *address, const char *filename);
int erase_cmd_add(struct list_head *ops, const char *address);

#endif
