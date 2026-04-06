/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __GPT_H__
#define __GPT_H__

struct qdl_device;

int gpt_find_by_name(struct qdl_device *qdl, const char *name, int *partition,
		     unsigned int *start_sector, unsigned int *num_sectors);

#endif
