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

#ifndef _OUTPUT_FILE_H_
#define _OUTPUT_FILE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <sparse/sparse.h>

struct output_file;

struct output_file* output_file_open_fd(int fd, unsigned int block_size, int64_t len, int gz,
                                        int sparse, int chunks, int crc, unsigned int read_timeout, unsigned int write_timeout);
struct output_file* output_file_open_callback(int (*write)(void*, const void*, size_t, unsigned int, unsigned int), void* priv,
                                              unsigned int block_size, int64_t len, int gz,
                                              int sparse, int chunks, int crc, unsigned int read_timeout, unsigned int write_timeout);
int write_data_chunk(struct output_file* out, unsigned int len, void* data, unsigned int read_timeout, unsigned int write_timeout);
int write_fill_chunk(struct output_file* out, unsigned int len, uint32_t fill_val, unsigned int read_timeout, unsigned int write_timeout);
int write_file_chunk(struct output_file* out, unsigned int len, const char* file, int64_t offset, unsigned int read_timeout, unsigned int write_timeout);
int write_fd_chunk(struct output_file* out, unsigned int len, int fd, int64_t offset, unsigned int read_timeout, unsigned int write_timeout);
int write_skip_chunk(struct output_file* out, int64_t len, unsigned int read_timeout, unsigned int write_timeout);
void output_file_close(struct output_file* out, unsigned int read_timeout, unsigned int write_timeout);

int read_all(int fd, void* buf, size_t lent);

#ifdef __cplusplus
}
#endif

#endif
