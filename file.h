/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __QDL_FILE_H__
#define __QDL_FILE_H__

#include <sys/types.h>

enum qdl_file_type {
	QDL_FILE_TYPE_UNKNOWN,
	QDL_FILE_TYPE_POSIX,
};

struct qdl_file {
	enum qdl_file_type type;

	size_t size;

	int fd;
};

int qdl_file_open(const char *filename, struct qdl_file *file);
void *qdl_file_load(struct qdl_file *file, size_t *len);
void qdl_file_close(struct qdl_file *file);
size_t qdl_file_getsize(struct qdl_file *file);
ssize_t qdl_file_read(struct qdl_file *file, void *buf, size_t len);
off_t qdl_file_seek(struct qdl_file *file, off_t offset, int whence);

#endif
