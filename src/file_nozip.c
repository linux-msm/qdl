// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include "qdl.h"
#include "file.h"
#include "flashmap.h"

/*
 * Built without zip-container support: qdl_zip_open() reports "not a
 * zip archive" for every input, so the plain contents.xml and
 * flashmap.json flows work unchanged and no zip handle ever exists.
 * The qdl_zip_file_*() helpers below are consequently unreachable, as
 * no qdl_file ever becomes QDL_FILE_TYPE_ZIP.
 */
bool qdl_zip_supported(void)
{
	return false;
}

int qdl_zip_open(const char *filename, struct qdl_zip **__qdl_zip)
{
	(void)filename;

	*__qdl_zip = NULL;

	return 0;
}

struct qdl_zip *qdl_zip_get(struct qdl_zip *qdl_zip)
{
	return qdl_zip;
}

void qdl_zip_put(struct qdl_zip *qdl_zip)
{
	(void)qdl_zip;
}

int qdl_zip_file_open(struct qdl_zip *qdl_zip, const char *filename,
		      struct qdl_file *file)
{
	(void)qdl_zip;
	(void)filename;
	(void)file;

	return -1;
}

ssize_t qdl_zip_file_read(struct qdl_file *file, void *buf, size_t len)
{
	(void)file;
	(void)buf;
	(void)len;

	return -1;
}

void qdl_zip_file_close(struct qdl_file *file)
{
	(void)file;
}

/*
 * zipper.c is only built with zip-container support; create-zip checks
 * qdl_zip_supported() and reports the missing feature before getting
 * here.
 */
int zipper_write(const char *filename, struct list_head *ops, struct sahara_image *images)
{
	(void)filename;
	(void)ops;
	(void)images;

	return -1;
}
