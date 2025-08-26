/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2010 The Android Open Source Project
 */
#ifndef __SPARSE_H__
#define __SPARSE_H__

#include <stdint.h>
#include <stdio.h>

typedef struct __attribute__((__packed__)) sparse_header {
	/* 0xed26ff3a */
	uint32_t magic;
	/* (0x1) - reject images with higher major versions */
	uint16_t major_version;
	/* (0x0) - allow images with higer minor versions */
	uint16_t minor_version;
	/* 28 bytes for first revision of the file format */
	uint16_t file_hdr_sz;
	/* 12 bytes for first revision of the file format */
	uint16_t chunk_hdr_sz;
	/* block size in bytes, must be a multiple of 4 (4096) */
	uint32_t blk_sz;
	/* total blocks in the non-sparse output image */
	uint32_t total_blks;
	/* total chunks in the sparse input image */
	uint32_t total_chunks;
	/*
	 * CRC32 checksum of the original data, counting "don't care"
	 * as 0. Standard 802.3 polynomial, use a Public Domain
	 * table implementation
	 */
	uint32_t image_checksum;
} sparse_header_t;

#define SPARSE_HEADER_MAGIC     0xed26ff3a
#define SPARSE_HEADER_MAJOR_VER 0x0001
#define SPARSE_HEADER_MINOR_VER 0x0000

typedef struct __attribute__((__packed__)) chunk_header {
	uint16_t chunk_type; /* 0xCAC1 -> raw; 0xCAC2 -> fill; 0xCAC3 -> don't care */
	uint16_t reserved1;
	uint32_t chunk_sz; /* in blocks in output image */
	uint32_t total_sz; /* in bytes of chunk input file including chunk header and data */
} chunk_header_t;

#define CHUNK_TYPE_RAW       0xCAC1
#define CHUNK_TYPE_FILL      0xCAC2
#define CHUNK_TYPE_DONT_CARE 0xCAC3

/*
 * Parses the sparse image header from the file descriptor.
 * Returns 0 on success, or an error code otherwise.
 */
int sparse_header_parse(int fd, sparse_header_t *sparse_header);

/*
 * Parses the sparse image chunk header from the file descriptor.
 * Sets the chunk size, and value or offset based on the parsed data.
 * Returns the chunk type on success, or an error code otherwise.
 */
int sparse_chunk_header_parse(int fd, sparse_header_t *sparse_header,
			      unsigned int *chunk_size,
			      uint32_t *value, off_t *offset);

#endif
