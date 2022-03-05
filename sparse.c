/*
 * Copyright (c) 2016, Bjorn Andersson <bjorn@kryo.se>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "sparse.h"

int sparse_header_parse(int fd, sparse_header_t* sparse_header)
{
	lseek(fd, 0, SEEK_SET);

	if (read(fd, sparse_header, sizeof(sparse_header_t)) != sizeof(sparse_header_t)) {
		fprintf(stderr, "[SPARSE] Unable to read sparse header\n");
		return -EINVAL;
	}

	if (htole32(sparse_header->magic) != htole32(SPARSE_HEADER_MAGIC)) {
		fprintf(stderr, "[SPARSE] Invalid magic in sparse header\n");
		return -EINVAL;
	}

	if (htole16(sparse_header->major_version) != htole16(SPARSE_HEADER_MAJOR_VER)) {
		fprintf(stderr, "[SPARSE] Invalid major version in sparse header\n");
		return -EINVAL;
	}

	if (htole16(sparse_header->minor_version) != htole16(SPARSE_HEADER_MINOR_VER)) {
		fprintf(stderr, "[SPARSE] Invalid minor version in sparse header\n");
		return -EINVAL;
	}

	if (sparse_header->file_hdr_sz > sizeof(sparse_header_t))
		lseek(fd, sparse_header->file_hdr_sz - sizeof(sparse_header_t), SEEK_CUR);

	return 0;
}

int sparse_chunk_header_parse(int fd, sparse_header_t* sparse_header, unsigned *chunk_size, unsigned *sparse_file_offset)
{
	chunk_header_t chunk_header;

	*chunk_size = 0;
	*sparse_file_offset = 0;

	if (read(fd, &chunk_header, sizeof(chunk_header_t)) != sizeof(chunk_header_t)) {
		fprintf(stderr, "[SPARSE] Unable to read sparse chunk header\n");
		return -EINVAL;
	}

	if (sparse_header->chunk_hdr_sz > sizeof(chunk_header_t))
		lseek(fd, sparse_header->chunk_hdr_sz - sizeof(chunk_header_t), SEEK_CUR);

	if (htole16(chunk_header.chunk_type) == htole16(CHUNK_TYPE_RAW)) {

		*chunk_size = chunk_header.chunk_sz * sparse_header->blk_sz;

		if (chunk_header.total_sz != (sparse_header->chunk_hdr_sz + *chunk_size)) {
			fprintf(stderr, "[SPARSE] Bogus chunk size, type Raw\n");
			return -EINVAL;
		}

		*sparse_file_offset = lseek(fd, 0, SEEK_CUR);
		lseek(fd, *chunk_size, SEEK_CUR);
	}
	else if (htole16(chunk_header.chunk_type) == htole16(CHUNK_TYPE_DONT_CARE)) {

		*chunk_size = chunk_header.chunk_sz * sparse_header->blk_sz;
		if (chunk_header.total_sz != sparse_header->chunk_hdr_sz) {
			fprintf(stderr, "[SPARSE] Bogus chunk size, type Don't Care\n");
			return -EINVAL;
		}
	}
	else if (htole16(chunk_header.chunk_type) == htole16(CHUNK_TYPE_FILL)) {
		fprintf(stderr, "[SPARSE] Fill chunk size doesn't support\n");
		return -EINVAL;
	}
	else {
		fprintf(stderr, "[SPARSE] Unknown chunk\n");
		return -EINVAL;
	}

	return 0;
}
