/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __QDL_FILE_H__
#define __QDL_FILE_H__

#include <stdbool.h>
#include <sys/types.h>

struct zip_file;

enum qdl_file_type {
	QDL_FILE_TYPE_UNKNOWN,
	QDL_FILE_TYPE_POSIX,
	QDL_FILE_TYPE_ZIP,
};

struct qdl_file {
	enum qdl_file_type type;

	size_t size;

	int fd;
	struct zip_file *zip_file;
};

struct qdl_zip;

int qdl_file_open(struct qdl_zip *qzip, const char *filename, struct qdl_file *file);
void *qdl_file_load(struct qdl_file *file, size_t *len);
void qdl_file_close(struct qdl_file *file);
size_t qdl_file_getsize(struct qdl_file *file);
ssize_t qdl_file_read(struct qdl_file *file, void *buf, size_t len);
ssize_t qdl_file_read_exact(struct qdl_file *file, void *buf, size_t len);
off_t qdl_file_seek(struct qdl_file *file, off_t offset, int whence);

/*
 * Implemented by file_zip.c, backed by libzip. Builds without
 * zip-container support compile file_nozip.c instead, whose
 * qdl_zip_open() never produces a zip handle, leaving the
 * qdl_zip_file_*() stubs unreachable as no qdl_file ever becomes
 * QDL_FILE_TYPE_ZIP.
 */
bool qdl_zip_supported(void);
int qdl_zip_open(const char *filename, struct qdl_zip **__qdl_zip);
struct qdl_zip *qdl_zip_get(struct qdl_zip *qzip);
void qdl_zip_put(struct qdl_zip *qzip);

int qdl_zip_file_open(struct qdl_zip *qzip, const char *filename, struct qdl_file *file);
ssize_t qdl_zip_file_read(struct qdl_file *file, void *buf, size_t len);
void qdl_zip_file_close(struct qdl_file *file);

#endif
