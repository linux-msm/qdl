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

#include <assert.h>
#include <stdlib.h>

#include "defs.h"
#include "sparse.h"
#include "sparse_file.h"

#include "backed_block.h"
#include "output_stream.h"
#include "sparse_defs.h"

struct sparse_file *sparse_file_new(unsigned int block_size, int64_t len)
{
	struct sparse_file *s = calloc(sizeof(struct sparse_file), 1);
	if (!s)
		return NULL;

	s->backed_block_list = backed_block_list_new(block_size);
	if (!s->backed_block_list) {
		free(s);
		return NULL;
	}

	s->block_size = block_size;
	s->len = len;

	return s;
}

void sparse_file_destroy(struct sparse_file *s)
{
	backed_block_list_destroy(s->backed_block_list);
	free(s);
}

int sparse_file_add_fill(struct sparse_file *s, uint32_t fill_val,
			 unsigned int len, unsigned int block)
{
	return backed_block_add_fill(s->backed_block_list, fill_val, len,
				     block);
}

int sparse_file_add_fd(struct sparse_file *s, int fd, int64_t file_offset,
		       unsigned int len, unsigned int block)
{
	return backed_block_add_fd(s->backed_block_list, fd, file_offset, len,
				   block);
}

static int sparse_file_write_block(struct output_stream *out,
				   struct backed_block *bb)
{
	int ret = -EINVAL;

	switch (backed_block_type(bb)) {
		case BACKED_BLOCK_FD:
			ret = write_fd_chunk(out, backed_block_len(bb),
					     backed_block_fd(bb),
					     backed_block_file_offset(bb));
			break;
		case BACKED_BLOCK_FILL:
			ret = write_fill_chunk(out, backed_block_len(bb),
					       backed_block_fill_val(bb));
			break;
	}

	return ret;
}

static int write_all_blocks(struct sparse_file *s, struct output_stream *out)
{
	struct backed_block *bb;
	unsigned int last_block = 0;
	int64_t pad;
	int ret = 0;

	for (bb = backed_block_iter_new(s->backed_block_list); bb;
	     bb = backed_block_iter_next(bb)) {
		if (backed_block_block(bb) > last_block) {
			unsigned int blocks =
			    backed_block_block(bb) - last_block;
			write_skip_chunk(out, (int64_t)blocks * s->block_size);
		}
		ret = sparse_file_write_block(out, bb);
		if (ret)
			return ret;
		last_block = backed_block_block(bb) +
			     DIV_ROUND_UP(backed_block_len(bb), s->block_size);
	}

	pad = s->len - (int64_t)last_block * s->block_size;
	assert(pad >= 0);
	if (pad > 0)
		write_skip_chunk(out, pad);

	return 0;
}

int sparse_stream_callback(struct sparse_file *s,
			   int (*write)(void *priv, const void *data,
					size_t len),
			   void *priv)
{
	int ret;
	struct output_stream *out;

	out = output_stream_open_callback(write, priv, s->block_size, s->len);
	if (!out)
		return -ENOMEM;

	ret = write_all_blocks(s, out);

	output_stream_close(out);

	return ret;
}

static int out_counter_write(void *priv, const void *data __unused, size_t len)
{
	int64_t *count = priv;
	*count += len;
	return 0;
}

int64_t sparse_file_len(struct sparse_file *s)
{
	int ret;
	int64_t count = 0;
	struct output_stream *out;

	out = output_stream_open_callback(out_counter_write, &count,
					  s->block_size, s->len);
	if (!out)
		return -ENOMEM;

	ret = write_all_blocks(s, out);

	output_stream_close(out);

	if (ret < 0)
		return -1;

	return count;
}
