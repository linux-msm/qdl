/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __GPT_H__
#define __GPT_H__

#include "list.h"

struct qdl_device;
struct list_head;

int gpt_find_by_name(struct qdl_device *qdl, const char *name, int *partition,
		     unsigned int *start_sector, unsigned int *num_sectors);
int gpt_resolve_deferrals(struct qdl_device *qdl, struct list_head *ops);

#endif
