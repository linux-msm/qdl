// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Maksim Paimushkin <maxim.paymushkin.development@gmail.com>
 * All rights reserved.
 */
#define _FILE_OFFSET_BITS 64
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "sparse.h"
#include "qdl.h"

int sparse_header_parse(int fd, sparse_header_t *sparse_header)
{
	lseek(fd, 0, SEEK_SET);

	if (read(fd, sparse_header, sizeof(sparse_header_t)) != sizeof(sparse_header_t)) {
		ux_err("[SPARSE] Unable to read sparse header\n");
		return -EINVAL;
	}

	if (sparse_header->magic != SPARSE_HEADER_MAGIC) {
		ux_err("[SPARSE] Invalid magic in sparse header\n");
		return -EINVAL;
	}

	if (sparse_header->major_version != SPARSE_HEADER_MAJOR_VER) {
		ux_err("[SPARSE] Invalid major version in sparse header\n");
		return -EINVAL;
	}

	if (sparse_header->minor_version != SPARSE_HEADER_MINOR_VER) {
		ux_err("[SPARSE] Invalid minor version in sparse header\n");
		return -EINVAL;
	}

	if (sparse_header->file_hdr_sz > sizeof(sparse_header_t))
		lseek(fd, sparse_header->file_hdr_sz - sizeof(sparse_header_t), SEEK_CUR);

	return 0;
}

int sparse_chunk_header_parse(int fd, sparse_header_t *sparse_header,
			      uint64_t *chunk_size,
			      uint32_t *value,
			      off_t *offset)
{
	chunk_header_t chunk_header;
	uint32_t fill_value = 0;
	unsigned int type;

	*chunk_size = 0;
	*value = 0;

	if (read(fd, &chunk_header, sizeof(chunk_header_t)) != sizeof(chunk_header_t)) {
		ux_err("[SPARSE] Unable to read sparse chunk header\n");
		return -EINVAL;
	}

	if (sparse_header->chunk_hdr_sz > sizeof(chunk_header_t))
		lseek(fd, sparse_header->chunk_hdr_sz - sizeof(chunk_header_t), SEEK_CUR);

	type = chunk_header.chunk_type;
	*chunk_size = (uint64_t)chunk_header.chunk_sz * sparse_header->blk_sz;

	switch (type) {
	case CHUNK_TYPE_RAW:
		if (chunk_header.total_sz != (sparse_header->chunk_hdr_sz + *chunk_size)) {
			ux_err("[SPARSE] Bogus chunk size, type Raw\n");
			return -EINVAL;
		}

		/* Save the current file offset in the 'value' variable */
		*offset = lseek(fd, 0, SEEK_CUR);

		/* Move the file cursor forward by the size of the chunk */
		lseek(fd, *chunk_size, SEEK_CUR);
		break;
	case CHUNK_TYPE_DONT_CARE:
		if (chunk_header.total_sz != sparse_header->chunk_hdr_sz) {
			ux_err("[SPARSE] Bogus chunk size, type Don't Care\n");
			return -EINVAL;
		}
		break;
	case CHUNK_TYPE_FILL:
		if (chunk_header.total_sz != (sparse_header->chunk_hdr_sz + sizeof(fill_value))) {
			ux_err("[SPARSE] Bogus chunk size, type Fill\n");
			return -EINVAL;
		}

		if (read(fd, &fill_value, sizeof(fill_value)) != sizeof(fill_value)) {
			ux_err("[SPARSE] Unable to read fill value\n");
			return -EINVAL;
		}

		/* Save the current fill value in the 'value' variable */
		*value = fill_value;
		break;
	default:
		ux_err("[SPARSE] Unknown chunk type: %#x\n", type);
		return -EINVAL;
	}

	return type;
}
