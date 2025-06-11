/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __VIP_H__
#define __VIP_H__

#include "sha2.h"

struct vip_table_generator;

enum vip_state {
	VIP_DISABLED,
	VIP_INIT,
	VIP_SEND_NEXT_TABLE,
	VIP_SEND_DATA,
	VIP_MAX,
};
#define MAX_CHAINED_FILES 32

struct vip_transfer_data {
	enum vip_state state;
	int signed_table_fd;
	int chained_fds[MAX_CHAINED_FILES];
	size_t chained_num;
	size_t chained_cur;
	size_t digests;
	size_t frames_sent;
	size_t frames_left;
	size_t chained_table_size;
	bool fh_parse_status;
};

int vip_transfer_init(struct qdl_device *qdl, const char *vip_table_path);
void vip_transfer_deinit(struct qdl_device *qdl);
int vip_transfer_handle_tables(struct qdl_device *qdl);
bool vip_transfer_status_check_needed(struct qdl_device *qdl);
void vip_transfer_clear_status(struct qdl_device *qdl);

int vip_gen_init(struct qdl_device *qdl, const char *path);
void vip_gen_chunk_init(struct qdl_device *qdl);
void vip_gen_chunk_update(struct qdl_device *qdl, const void *buf, size_t len);
void vip_gen_chunk_store(struct qdl_device *qdl);
void vip_gen_finalize(struct qdl_device *qdl);

#endif /* __VIP_H__ */
