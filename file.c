// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "oscompat.h"
#include "qdl.h"
#include "file.h"

int qdl_file_open(const char *filename, struct qdl_file *file)
{
	off_t len;
	int fd;

	fd = open(filename, O_RDONLY | O_BINARY);
	if (fd < 0) {
		ux_err("failed to open \"%s\" for reading\n", filename);
		return -1;
	}

	len = lseek(fd, 0, SEEK_END);
	if (len < 0) {
		ux_err("failed to find end of \"%s\"\n", filename);
		close(fd);
		return -1;
	}

	lseek(fd, 0, SEEK_SET);

	file->type = QDL_FILE_TYPE_POSIX;
	file->fd = fd;
	file->size = len;

	return 0;
}

void *qdl_file_load(struct qdl_file *file, size_t *len)
{
	ssize_t n;
	void *buf;

	buf = malloc(file->size);
	if (!buf) {
		ux_err("unable to allocate memory to load file\n");
		return NULL;
	}

	switch (file->type) {
	case QDL_FILE_TYPE_UNKNOWN:
		ux_err("internal error: attempting to load unknown file type\n");
		return NULL;
	case QDL_FILE_TYPE_POSIX:
		n = read(file->fd, buf, file->size);
		if ((size_t)n != file->size) {
			ux_err("failed to load content of file\n");
			goto err_free_buf;
		}
		break;
	};

	*len = file->size;
	return buf;

err_free_buf:
	free(buf);
	return NULL;
}

void qdl_file_close(struct qdl_file *file)
{
	switch (file->type) {
	case QDL_FILE_TYPE_UNKNOWN:
		break;
	case QDL_FILE_TYPE_POSIX:
		close(file->fd);
		file->fd = -1;
		break;
	};

	file->type = QDL_FILE_TYPE_UNKNOWN;
}

size_t qdl_file_getsize(struct qdl_file *file)
{
	return file->size;
}

ssize_t qdl_file_read(struct qdl_file *file, void *buf, size_t len)
{
	switch (file->type) {
	case QDL_FILE_TYPE_UNKNOWN:
		break;
	case QDL_FILE_TYPE_POSIX:
		return read(file->fd, buf, len);
	};

	return -1;
}

off_t qdl_file_seek(struct qdl_file *file, off_t offset, int whence)
{
	switch (file->type) {
	case QDL_FILE_TYPE_UNKNOWN:
		return -1;
	case QDL_FILE_TYPE_POSIX:
		return lseek(file->fd, offset, whence);
	};

	return -1;
}
