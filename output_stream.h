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

#ifndef __OUTPUT_STREAM_H__
#define __OUTPUT_STREAM_H__

#include "sparse.h"

struct output_stream;

struct output_stream *
output_stream_open_callback(int (*write)(void *, const void *, size_t),
			    void *priv, unsigned int block_size, int64_t len);

int write_fill_chunk(struct output_stream *out, unsigned int len,
		     uint32_t fill_val);
int write_fd_chunk(struct output_stream *out, unsigned int len, int fd,
		   int64_t offset);
int write_skip_chunk(struct output_stream *out, int64_t len);
void output_stream_close(struct output_stream *out);

int read_all(int fd, void *buf, size_t len);

#endif
