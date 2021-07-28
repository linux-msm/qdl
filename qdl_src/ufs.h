/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
	int (*apply_ufs_common)(struct qdl_device *qdl, struct ufs_common *ufs, unsigned int read_timeout, unsigned int write_timeout),
	int (*apply_ufs_body)(struct qdl_device *qdl, struct ufs_body *ufs, unsigned int read_timeout, unsigned int write_timeout),
	int (*apply_ufs_epilogue)(struct qdl_device *qdl, struct ufs_epilogue *ufs, bool commit, unsigned intread_timeout, unsigned int write_timeout),
	unsigned int read_timeout, unsigned int write_timeout);
bool ufs_need_provisioning(void);

#endif
