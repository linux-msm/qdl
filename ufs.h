/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */
#ifndef __UFS_H__
#define __UFS_H__
#include <stdbool.h>

struct qdl_device;

struct ufs_common {
	unsigned	bNumberLU;
	bool		bBootEnable;
	bool		bDescrAccessEn;
	unsigned	bInitPowerMode;
	unsigned	bHighPriorityLUN;
	unsigned	bSecureRemovalType;
	unsigned	bInitActiveICCLevel;
	unsigned	wPeriodicRTCUpdate;
	bool		bConfigDescrLock;
	bool            wb;
	bool            bWriteBoosterBufferPreserveUserSpaceEn;
	bool            bWriteBoosterBufferType;
	unsigned        shared_wb_buffer_size_in_kb;
};

struct ufs_body {
	unsigned	LUNum;
	bool		bLUEnable;
	unsigned	bBootLunID;
	unsigned	size_in_kb;
	unsigned	bDataReliability;
	unsigned	bLUWriteProtect;
	unsigned	bMemoryType;
	unsigned	bLogicalBlockSize;
	unsigned	bProvisioningType;
	unsigned	wContextCapabilities;
	const char	*desc;

	struct		ufs_body *next;
};

struct ufs_epilogue {
	unsigned	LUNtoGrow;
	bool		commit;
};

int ufs_load(const char *ufs_file, bool finalize_provisioning);
int ufs_provisioning_execute(struct qdl_device *qdl,
	int (*apply_ufs_common)(struct qdl_device *qdl, struct ufs_common *ufs),
	int (*apply_ufs_body)(struct qdl_device *qdl, struct ufs_body *ufs),
	int (*apply_ufs_epilogue)(struct qdl_device *qdl, struct ufs_epilogue *ufs, bool commit));
bool ufs_need_provisioning(void);

#endif
