// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <zip.h>

#include "oscompat.h"
#include "qdl.h"
#include "file.h"

struct qdl_zip {
	zip_t *zip;
	unsigned int refcount;
};

int qdl_file_open(struct qdl_zip *qdl_zip, const char *filename, struct qdl_file *file)
{
	struct zip_stat st;
	zip_int64_t idx;
	zip_file_t *zf;
	off_t len;
	zip_t *zip;
	int fd;

	if (qdl_zip) {
		zip = qdl_zip->zip;

		idx = zip_name_locate(zip, filename, 0);
		if (idx < 0) {
			ux_err("unable to locate \"%s\" in zip archive\n", filename);
			return -1;
		}

		if (zip_stat_index(zip, idx, 0, &st) < 0) {
			ux_err("unable to stat \"%s\" in zip archive\n", filename);
			return -1;
		}

		zf = zip_fopen_index(zip, idx, 0);
		if (!zf) {
			ux_err("unable to open \"%s\" in zip archive\n", filename);
			return -1;
		}

		file->type = QDL_FILE_TYPE_ZIP;
		file->fd = -1;
		file->size = st.size;
		file->zip_file = zf;
	} else {
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
		file->zip_file = NULL;
	}

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
	case QDL_FILE_TYPE_ZIP:
		n = zip_fread(file->zip_file, buf, file->size);
		if ((size_t)n != file->size) {
			ux_err("failed to load zip file member\n");
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
	case QDL_FILE_TYPE_ZIP:
		zip_fclose(file->zip_file);
		file->zip_file = NULL;
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
	case QDL_FILE_TYPE_ZIP:
		return zip_fread(file->zip_file, buf, len);
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
	case QDL_FILE_TYPE_ZIP:
		if (offset == 0 && whence == SEEK_SET)
			return 0;

		ux_err("seek not implemented for zip files\n");
		return -1;
	};

	return -1;
}

int qdl_zip_open(const char *filename, struct qdl_zip **__qdl_zip)
{
	struct qdl_zip *qdl_zip;
	zip_t *zip;

	zip = zip_open(filename, ZIP_RDONLY, NULL);
	if (!zip) {
		*__qdl_zip = NULL;
		return 0;
	}

	qdl_zip = calloc(1, sizeof(*qdl_zip));
	if (!qdl_zip) {
		zip_close(zip);
		return -1;
	}

	qdl_zip->zip = zip;
	qdl_zip->refcount = 1;

	*__qdl_zip = qdl_zip;

	return 0;
}

struct qdl_zip *qdl_zip_get(struct qdl_zip *qdl_zip)
{
	if (qdl_zip)
		qdl_zip->refcount++;

	return qdl_zip;
}

void qdl_zip_put(struct qdl_zip *qdl_zip)
{
	if (qdl_zip) {
		if (--qdl_zip->refcount == 0) {
			zip_close(qdl_zip->zip);
			free(qdl_zip);
		}
	}
}
