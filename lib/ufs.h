/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */
#ifndef __UFS_H__
#define __UFS_H__
#include <stdbool.h>

#include "list.h"

struct qdl_device;

struct ufs_common {
	unsigned int bNumberLU;
	bool		bBootEnable;
	bool		bDescrAccessEn;
	unsigned int bInitPowerMode;
	unsigned int bHighPriorityLUN;
	unsigned int bSecureRemovalType;
	unsigned int bInitActiveICCLevel;
	unsigned int wPeriodicRTCUpdate;
	bool		bConfigDescrLock;
	bool            wb;
	bool            bWriteBoosterBufferPreserveUserSpaceEn;
	bool            bWriteBoosterBufferType;
	unsigned int shared_wb_buffer_size_in_kb;
};

struct ufs_body {
	unsigned int LUNum;
	bool		bLUEnable;
	unsigned int bBootLunID;
	unsigned int size_in_kb;
	unsigned int bDataReliability;
	unsigned int bLUWriteProtect;
	unsigned int bMemoryType;
	unsigned int bLogicalBlockSize;
	unsigned int bProvisioningType;
	unsigned int wContextCapabilities;
	const char	*desc;

	struct list_head node;
};

struct ufs_epilogue {
	unsigned int LUNtoGrow;
	bool		commit;
};

int ufs_load(const char *ufs_file, bool finalize_provisioning);
int ufs_provisioning_execute(struct qdl_device *qdl,
			     int (*apply_ufs_common)(struct qdl_device *qdl, struct ufs_common *ufs),
			     int (*apply_ufs_body)(struct qdl_device *qdl, struct ufs_body *ufs),
			     int (*apply_ufs_epilogue)(struct qdl_device *qdl, struct ufs_epilogue *ufs, bool commit));
bool ufs_need_provisioning(void);

#endif
