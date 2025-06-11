/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __SIM_H__
#define __SIM_H__

#include "qdl.h"
#include "vip.h"

struct vip_table_generator *sim_get_vip_generator(struct qdl_device *qdl);
bool sim_set_digest_generation(bool create_digests, struct qdl_device *qdl,
                               struct vip_table_generator *vip_gen);

#endif /* __SIM_H__ */