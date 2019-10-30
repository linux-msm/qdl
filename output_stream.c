/*
 * Copyright (C) 2010 The Android Open Source Project
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

#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE 1

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include "output_stream.h"
#include "sparse_format.h"

#ifndef USE_MINGW
#include <sys/mman.h>
#define O_BINARY 0
#else
#define ftruncate64 ftruncate
#endif

#if defined(__APPLE__) && defined(__MACH__)
#define lseek64 lseek
#define ftruncate64 ftruncate
#define mmap64 mmap
#define off64_t off_t
#endif

#define min(a, b)                                                              \
	({                                                                     \
		typeof(a) _a = (a);                                            \
		typeof(b) _b = (b);                                            \
		(_a < _b) ? _a : _b;                                           \
	})

#define SPARSE_HEADER_MAJOR_VER 1
#define SPARSE_HEADER_MINOR_VER 0
#define SPARSE_HEADER_LEN (sizeof(sparse_header_t))
#define CHUNK_HEADER_LEN (sizeof(chunk_header_t))

#define container_of(inner, outer_t, elem)                                     \
	((outer_t *)((char *)(inner)-offsetof(outer_t, elem)))

struct output_stream_ops {
	int (*open)(struct output_stream *, int fd);
	int (*skip)(struct output_stream *, int64_t);
	int (*pad)(struct output_stream *, int64_t);
	int (*write)(struct output_stream *, void *, size_t);
	void (*close)(struct output_stream *);
};

struct sparse_stream_ops {
	int (*write_data_chunk)(struct output_stream *out, unsigned int len,
				void *data);
	int (*write_fill_chunk)(struct output_stream *out, unsigned int len,
				uint32_t fill_val);
	int (*write_skip_chunk)(struct output_stream *out, int64_t len);
	int (*write_end_chunk)(struct output_stream *out);
};

struct output_stream {
	int64_t cur_out_ptr;
	unsigned int chunk_cnt;
	struct output_stream_ops *ops;
	struct sparse_stream_ops *sparse_ops;
	unsigned int block_size;
	int64_t len;
	char *zero_buf;
	uint32_t *fill_buf;
	char *buf;
};

struct output_stream_callback {
	struct output_stream out;
	void *priv;
	int (*write)(void *priv, const void *buf, size_t len);
};

#define to_output_stream_callback(_o)                                          \
	container_of((_o), struct output_stream_callback, out)

static int callback_stream_open()
{
	return 0;
}

static int callback_stream_skip(struct output_stream *out, int64_t off)
{
	struct output_stream_callback *outc = to_output_stream_callback(out);
	int to_write;
	int ret;
	void *data;

	while (off > 0) {
		to_write = min(off, (int64_t)INT_MAX);
		data = calloc(to_write, 1);
		ret = outc->write(outc->priv, data, to_write);
		free(data);
		if (ret < 0) {
			return ret;
		}
		off -= to_write;
	}

	return 0;
}

static int callback_stream_pad()
{
	return -1;
}

static int callback_stream_write(struct output_stream *out, void *data,
				 size_t len)
{
	struct output_stream_callback *outc = to_output_stream_callback(out);

	return outc->write(outc->priv, data, len);
}

static void callback_stream_close(struct output_stream *out)
{
	struct output_stream_callback *outc = to_output_stream_callback(out);

	if (outc->out.fill_buf)
		free(outc->out.fill_buf);
	if (outc->out.zero_buf)
		free(outc->out.zero_buf);

	free(outc);
}

static struct output_stream_ops callback_stream_ops = {
    .open = callback_stream_open,
    .skip = callback_stream_skip,
    .pad = callback_stream_pad,
    .write = callback_stream_write,
    .close = callback_stream_close,
};

int read_all(int fd, void *buf, size_t len)
{
	size_t total = 0;
	int ret;
	char *ptr = buf;

	while (total < len) {
		ret = read(fd, ptr, len - total);

		if (ret < 0)
			return -errno;

		if (ret == 0)
			return -EINVAL;

		ptr += ret;
		total += ret;
	}

	return 0;
}

static int write_normal_data_chunk(struct output_stream *out, unsigned int len,
				   void *data)
{
	int ret;
	unsigned int rnd_up_len = ALIGN(len, out->block_size);

	ret = out->ops->write(out, data, len);
	if (ret < 0)
		return ret;

	if (rnd_up_len > len)
		ret = out->ops->skip(out, rnd_up_len - len);

	return ret;
}

static int write_normal_fill_chunk(struct output_stream *out, unsigned int len,
				   uint32_t fill_val)
{
	int ret;
	unsigned int i;
	unsigned int write_len;

	/* Initialize fill_buf with the fill_val */
	for (i = 0; i < out->block_size / sizeof(uint32_t); i++)
		out->fill_buf[i] = fill_val;

	while (len) {
		write_len = min(len, out->block_size);
		ret = out->ops->write(out, out->fill_buf, write_len);
		if (ret < 0)
			return ret;

		len -= write_len;
	}

	return 0;
}

static int write_normal_skip_chunk(struct output_stream *out, int64_t len)
{
	return out->ops->skip(out, len);
}

int write_normal_end_chunk(struct output_stream *out)
{
	return out->ops->pad(out, out->len);
}

static struct sparse_stream_ops normal_stream_ops = {
    .write_data_chunk = write_normal_data_chunk,
    .write_fill_chunk = write_normal_fill_chunk,
    .write_skip_chunk = write_normal_skip_chunk,
    .write_end_chunk = write_normal_end_chunk,
};

void output_stream_close(struct output_stream *out)
{
	out->sparse_ops->write_end_chunk(out);

	out->ops->close(out);
}

static int output_stream_init(struct output_stream *out, int block_size,
			      int64_t len)
{
	int ret;

	out->len = len;
	out->block_size = block_size;
	out->cur_out_ptr = 0LL;
	out->chunk_cnt = 0;

	out->zero_buf = calloc(block_size, 1);
	if (!out->zero_buf) {
		error_errno("malloc zero_buf");
		return -ENOMEM;
	}

	out->fill_buf = calloc(block_size, 1);
	if (!out->fill_buf) {
		error_errno("malloc fill_buf");
		ret = -ENOMEM;
		goto err_fill_buf;
	}

	out->sparse_ops = &normal_stream_ops;

	return 0;

err_fill_buf:
	free(out->zero_buf);
	return ret;
}

struct output_stream *
output_stream_open_callback(int (*write)(void *, const void *, size_t),
			    void *priv, unsigned int block_size, int64_t len)
{
	int ret;
	struct output_stream_callback *outc;

	outc = calloc(1, sizeof(struct output_stream_callback));
	if (!outc) {
		error_errno("malloc struct outc");
		return NULL;
	}

	outc->out.ops = &callback_stream_ops;
	outc->priv = priv;
	outc->write = write;

	ret = output_stream_init(&outc->out, block_size, len);
	if (ret < 0) {
		free(outc);
		return NULL;
	}

	return &outc->out;
}

/* Write a contiguous region of data blocks with a fill value */
int write_fill_chunk(struct output_stream *out, unsigned int len,
		     uint32_t fill_val)
{
	return out->sparse_ops->write_fill_chunk(out, len, fill_val);
}

int write_fd_chunk(struct output_stream *out, unsigned int len, int fd,
		   int64_t offset)
{
	int ret;
	int64_t aligned_offset;
	int aligned_diff;
	uint64_t buffer_size;
	char *ptr;

#ifdef _SC_PAGESIZE
	aligned_offset = offset & ~(sysconf(_SC_PAGESIZE) - 1);
#else
	aligned_offset = offset & ~(4096 - 1);
#endif
	aligned_diff = offset - aligned_offset;
	buffer_size = (uint64_t)len + (uint64_t)aligned_diff;

#ifndef USE_MINGW
	if (buffer_size > SIZE_MAX)
		return -E2BIG;
	char *data = mmap64(NULL, buffer_size, PROT_READ, MAP_SHARED, fd,
			    aligned_offset);
	if (data == MAP_FAILED)
		return -errno;

	ptr = data + aligned_diff;
#else
	off64_t pos;
	char *data = malloc(len);
	if (!data)
		return -errno;
	pos = lseek64(fd, offset, SEEK_SET);
	if (pos < 0) {
		free(data);
		return -errno;
	}
	ret = read_all(fd, data, len);
	if (ret < 0) {
		free(data);
		return ret;
	}
	ptr = data;
#endif

	ret = out->sparse_ops->write_data_chunk(out, len, ptr);

#ifndef USE_MINGW
	munmap(data, buffer_size);
#else
	free(data);
#endif

	return ret;
}

int write_skip_chunk(struct output_stream *out, int64_t len)
{
	return out->sparse_ops->write_skip_chunk(out, len);
}
