/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2019, Linaro Ltd.
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
#ifndef __JSON_H__
#define __JSON_H__

#include <stddef.h>

#define JSON_ERROR_SIZE 256
#define JSON_MAX_DEPTH 256

enum {
	JSON_TYPE_UNKNOWN,
	JSON_TYPE_TRUE,
	JSON_TYPE_FALSE,
	JSON_TYPE_NULL,
	JSON_TYPE_NUMBER,
	JSON_TYPE_STRING,
	JSON_TYPE_ARRAY,
	JSON_TYPE_OBJECT,
};

struct json_value {
	const char *key;

	int type;
	union {
		double number;
		const char *string;
		struct json_value *value;
	} u;

	struct json_value *next;
};

/*
 * Parser error message for the most recent json_parse_buf() call.
 * Empty string means no parse error was recorded.
 * This buffer is global and not thread-safe.
 */
extern char json_error[JSON_ERROR_SIZE];

struct json_value *json_parse_buf(const char *json, size_t len);
int json_count_children(struct json_value *array);
/* Duplicate object keys are resolved by first match in input order. */
struct json_value *json_get_child(struct json_value *object, const char *key);
struct json_value *json_get_element_object(struct json_value *object, unsigned int idx);
const char *json_get_element_string(struct json_value *object, unsigned int idx);
int json_get_number(struct json_value *object, const char *key, double *number);
const char *json_get_string(struct json_value *object, const char *key);
void json_free(struct json_value *value);

#endif
