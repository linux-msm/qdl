/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __VIP_H__
#define __VIP_H__

#include "sha2.h"

struct vip_table_generator;

int vip_gen_init(struct qdl_device *qdl, const char *path);
void vip_gen_chunk_init(struct qdl_device *qdl);
void vip_gen_chunk_update(struct qdl_device *qdl, const void *buf, size_t len);
void vip_gen_chunk_store(struct qdl_device *qdl);
void vip_gen_finalize(struct qdl_device *qdl);

#endif /* __VIP_H__ */
