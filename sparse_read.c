/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE 1

#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sparse.h"

#include "output_stream.h"
#include "sparse_file.h"
#include "sparse_format.h"

#if defined(__APPLE__) && defined(__MACH__)
#define lseek64 lseek
#define off64_t off_t
#endif

#define SPARSE_HEADER_MAJOR_VER 1
#define SPARSE_HEADER_LEN (sizeof(sparse_header_t))
#define CHUNK_HEADER_LEN (sizeof(chunk_header_t))

#define min(a, b)                                                              \
	({                                                                     \
		typeof(a) _a = (a);                                            \
		typeof(b) _b = (b);                                            \
		(_a < _b) ? _a : _b;                                           \
	})

static void verbose_error(bool verbose, int err, const char *fmt, ...)
{
	char *s = "";
	char *at = "";
	if (fmt) {
		va_list argp;
		int size;

		va_start(argp, fmt);
		size = vsnprintf(NULL, 0, fmt, argp);
		va_end(argp);

		if (size < 0)
			return;

		at = malloc(size + 1);
		if (at == NULL)
			return;

		va_start(argp, fmt);
		vsnprintf(at, size, fmt, argp);
		va_end(argp);
		at[size] = 0;
		s = " at ";
	}
	if (verbose) {
#ifndef USE_MINGW
		if (err == -EOVERFLOW) {
			sparse_print_verbose("EOF while reading file%s%s\n", s,
					     at);
		} else
#endif
		    if (err == -EINVAL)
			sparse_print_verbose("Invalid sparse file format%s%s\n",
					     s, at);
		else if (err == -ENOMEM)
			sparse_print_verbose(
			    "Failed allocation while reading file%s%s\n", s,
			    at);
		else
			sparse_print_verbose("Unknown error %d%s%s\n", err, s,
					     at);
	}
	if (fmt)
		free(at);
}

static int process_raw_chunk(struct sparse_file *s, unsigned int chunk_size,
			     int fd, int64_t offset, unsigned int blocks,
			     unsigned int block)
{
	int ret;
	unsigned int len = blocks * s->block_size;

	if (chunk_size % s->block_size != 0)
		return -EINVAL;

	if (chunk_size / s->block_size != blocks)
		return -EINVAL;

	ret = sparse_file_add_fd(s, fd, offset, len, block);
	if (ret < 0)
		return ret;

	lseek64(fd, len, SEEK_CUR);

	return 0;
}

static int process_fill_chunk(struct sparse_file *s, unsigned int chunk_size,
			      int fd, unsigned int blocks, unsigned int block)
{
	int ret;
	int64_t len = (int64_t)blocks * s->block_size;
	uint32_t fill_val;

	if (chunk_size != sizeof(fill_val))
		return -EINVAL;

	ret = read_all(fd, &fill_val, sizeof(fill_val));
	if (ret < 0)
		return ret;

	ret = sparse_file_add_fill(s, fill_val, len, block);
	if (ret < 0)
		return ret;

	return 0;
}

static int process_skip_chunk(unsigned int chunk_size)
{
	if (chunk_size != 0)
		return -EINVAL;

	return 0;
}

static int process_chunk(struct sparse_file *s, int fd, off64_t offset,
			 unsigned int chunk_hdr_sz,
			 chunk_header_t *chunk_header, unsigned int cur_block)
{
	int ret;
	unsigned int chunk_data_size;

	chunk_data_size = chunk_header->total_sz - chunk_hdr_sz;

	switch (chunk_header->chunk_type) {
		case CHUNK_TYPE_RAW:
			ret = process_raw_chunk(s, chunk_data_size, fd, offset,
						chunk_header->chunk_sz,
						cur_block);
			if (ret < 0) {
				verbose_error(s->verbose, ret,
					      "data block at %" PRId64, offset);
				return ret;
			}

			return chunk_header->chunk_sz;
		case CHUNK_TYPE_FILL:
			ret = process_fill_chunk(s, chunk_data_size, fd,
						 chunk_header->chunk_sz,
						 cur_block);
			if (ret < 0) {
				verbose_error(s->verbose, ret,
					      "fill block at %" PRId64, offset);
				return ret;
			}

			return chunk_header->chunk_sz;
		case CHUNK_TYPE_DONT_CARE:
			ret = process_skip_chunk(chunk_data_size);
			if (chunk_data_size != 0) {
				if (ret < 0) {
					verbose_error(s->verbose, ret,
						      "skip block at %" PRId64,
						      offset);
					return ret;
				}
			}
			return chunk_header->chunk_sz;
		default:
			verbose_error(s->verbose, -EINVAL,
				      "unknown block %04X at %" PRId64,
				      chunk_header->chunk_type, offset);
	}

	return 0;
}

static int sparse_file_read_sparse(struct sparse_file *s, int fd)
{
	int ret;
	unsigned int i;
	sparse_header_t sparse_header;
	chunk_header_t chunk_header;
	unsigned int cur_block = 0;
	off64_t offset;

	ret = read_all(fd, &sparse_header, sizeof(sparse_header));
	if (ret < 0)
		return ret;

	if (sparse_header.magic != SPARSE_HEADER_MAGIC)
		return -EINVAL;

	if (sparse_header.major_version != SPARSE_HEADER_MAJOR_VER)
		return -EINVAL;

	if (sparse_header.file_hdr_sz < SPARSE_HEADER_LEN)
		return -EINVAL;

	if (sparse_header.chunk_hdr_sz < sizeof(chunk_header))
		return -EINVAL;

	if (sparse_header.file_hdr_sz > SPARSE_HEADER_LEN)
		/* Skip the remaining bytes in a header that is longer than
		 * we expected.
		 */
		lseek64(fd, sparse_header.file_hdr_sz - SPARSE_HEADER_LEN,
			SEEK_CUR);

	for (i = 0; i < sparse_header.total_chunks; i++) {
		ret = read_all(fd, &chunk_header, sizeof(chunk_header));
		if (ret < 0)
			return ret;

		if (sparse_header.chunk_hdr_sz > CHUNK_HEADER_LEN)
			/* Skip the remaining bytes in a header that is longer
			 * than we expected.
			 */
			lseek64(fd,
				sparse_header.chunk_hdr_sz - CHUNK_HEADER_LEN,
				SEEK_CUR);

		offset = lseek64(fd, 0, SEEK_CUR);

		ret = process_chunk(s, fd, offset, sparse_header.chunk_hdr_sz,
				    &chunk_header, cur_block);
		if (ret < 0)
			return ret;

		cur_block += ret;
	}

	if (sparse_header.total_blks != cur_block)
		return -EINVAL;

	return 0;
}

struct sparse_file *sparse_file_import(int fd, bool verbose)
{
	int ret;
	sparse_header_t sparse_header;
	int64_t len;
	struct sparse_file *s;

	ret = read_all(fd, &sparse_header, sizeof(sparse_header));
	if (ret < 0) {
		verbose_error(verbose, ret, "header");
		return NULL;
	}

	if (sparse_header.magic != SPARSE_HEADER_MAGIC) {
		verbose_error(verbose, -EINVAL, "header magic");
		return NULL;
	}

	if (sparse_header.major_version != SPARSE_HEADER_MAJOR_VER) {
		verbose_error(verbose, -EINVAL, "header major version");
		return NULL;
	}

	if (sparse_header.file_hdr_sz < SPARSE_HEADER_LEN)
		return NULL;

	if (sparse_header.chunk_hdr_sz < sizeof(chunk_header_t))
		return NULL;

	len = (int64_t)sparse_header.total_blks * sparse_header.blk_sz;
	s = sparse_file_new(sparse_header.blk_sz, len);
	if (!s) {
		verbose_error(verbose, -EINVAL, NULL);
		return NULL;
	}

	ret = lseek64(fd, 0, SEEK_SET);
	if (ret < 0) {
		verbose_error(verbose, ret, "seeking");
		sparse_file_destroy(s);
		return NULL;
	}

	ret = sparse_file_read_sparse(s, fd);
	if (ret < 0) {
		sparse_file_destroy(s);
		return NULL;
	}

	return s;
}
