// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <stdlib.h>
#include <zip.h>

#include "qdl.h"
#include "file.h"

struct qdl_zip {
	zip_t *zip;
	unsigned int refcount;
};

bool qdl_zip_supported(void)
{
	return true;
}

int qdl_zip_file_open(struct qdl_zip *qdl_zip, const char *filename,
		      struct qdl_file *file)
{
	struct zip_stat st;
	zip_int64_t idx;
	zip_file_t *zf;
	zip_t *zip = qdl_zip->zip;

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

	return 0;
}

ssize_t qdl_zip_file_read(struct qdl_file *file, void *buf, size_t len)
{
	return zip_fread(file->zip_file, buf, len);
}

void qdl_zip_file_close(struct qdl_file *file)
{
	zip_fclose(file->zip_file);
	file->zip_file = NULL;
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
	if (!qdl_zip)
		return;

	if (--qdl_zip->refcount == 0) {
		zip_close(qdl_zip->zip);
		free(qdl_zip);
	}
}
