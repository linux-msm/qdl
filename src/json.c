// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2018-2019, Linaro Ltd.
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
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

static const char *input_buf;
static int input_pos;
static int input_len;
static bool input_can_unput;
static int nesting_depth;
char json_error[JSON_ERROR_SIZE];
enum { JSON_INPUT_EOF = -1 };

static int json_parse_array(struct json_value *array);
static int json_parse_object(struct json_value *object);
static int json_parse_property(struct json_value *value);
static int input(void);
static void unput(void);

static void json_set_error(const char *fmt, ...)
{
	va_list ap;

	if (json_error[0])
		return;

	va_start(ap, fmt);
	vsnprintf(json_error, sizeof(json_error), fmt, ap);
	va_end(ap);
}

static int json_hex_value(int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}

static int json_buf_append(char **buf, size_t *len, size_t *cap, unsigned char byte)
{
	char *new_buf;

	if (*len + 1 >= *cap) {
		size_t new_cap = *cap ? (*cap * 2) : 32;

		new_buf = realloc(*buf, new_cap);
		if (!new_buf)
			return -1;

		*buf = new_buf;
		*cap = new_cap;
	}

	(*buf)[*len] = (char)byte;
	(*len)++;

	return 0;
}

static int json_buf_append_utf8(char **buf, size_t *len, size_t *cap, unsigned int cp)
{
	if (cp <= 0x7f)
		return json_buf_append(buf, len, cap, (unsigned char)cp);
	if (cp <= 0x7ff) {
		if (json_buf_append(buf, len, cap, (unsigned char)(0xc0 | (cp >> 6))))
			return -1;
		return json_buf_append(buf, len, cap, (unsigned char)(0x80 | (cp & 0x3f)));
	}
	if (cp <= 0xffff) {
		if (json_buf_append(buf, len, cap, (unsigned char)(0xe0 | (cp >> 12))))
			return -1;
		if (json_buf_append(buf, len, cap, (unsigned char)(0x80 | ((cp >> 6) & 0x3f))))
			return -1;
		return json_buf_append(buf, len, cap, (unsigned char)(0x80 | (cp & 0x3f)));
	}
	if (cp <= 0x10ffff) {
		if (json_buf_append(buf, len, cap, (unsigned char)(0xf0 | (cp >> 18))))
			return -1;
		if (json_buf_append(buf, len, cap, (unsigned char)(0x80 | ((cp >> 12) & 0x3f))))
			return -1;
		if (json_buf_append(buf, len, cap, (unsigned char)(0x80 | ((cp >> 6) & 0x3f))))
			return -1;
		return json_buf_append(buf, len, cap, (unsigned char)(0x80 | (cp & 0x3f)));
	}

	return -1;
}

static int json_parse_hex4(unsigned int *out)
{
	unsigned int code = 0;
	int i;

	for (i = 0; i < 4; i++) {
		int ch = input();
		int digit;

		if (ch == JSON_INPUT_EOF)
			return -1;

		digit = json_hex_value(ch);
		if (digit < 0)
			return -1;

		code = (code << 4) | (unsigned int)digit;
	}

	*out = code;
	return 0;
}

static int json_parse_escape(char **buf, size_t *len, size_t *cap, int escape_pos)
{
	unsigned int code;
	unsigned int low;
	int ch;

	ch = input();
	if (ch == JSON_INPUT_EOF) {
		json_set_error("unterminated escape at offset %d", escape_pos);
		return -1;
	}

	switch (ch) {
	case '"':
	case '\\':
	case '/':
		if (json_buf_append(buf, len, cap, (unsigned char)ch)) {
			json_set_error("out of memory while parsing escape at offset %d", escape_pos);
			return -1;
		}
		return 0;
	case 'b':
		if (json_buf_append(buf, len, cap, '\b')) {
			json_set_error("out of memory while parsing escape at offset %d", escape_pos);
			return -1;
		}
		return 0;
	case 'f':
		if (json_buf_append(buf, len, cap, '\f')) {
			json_set_error("out of memory while parsing escape at offset %d", escape_pos);
			return -1;
		}
		return 0;
	case 'n':
		if (json_buf_append(buf, len, cap, '\n')) {
			json_set_error("out of memory while parsing escape at offset %d", escape_pos);
			return -1;
		}
		return 0;
	case 'r':
		if (json_buf_append(buf, len, cap, '\r')) {
			json_set_error("out of memory while parsing escape at offset %d", escape_pos);
			return -1;
		}
		return 0;
	case 't':
		if (json_buf_append(buf, len, cap, '\t')) {
			json_set_error("out of memory while parsing escape at offset %d", escape_pos);
			return -1;
		}
		return 0;
	case 'u':
		if (json_parse_hex4(&code)) {
			json_set_error("invalid unicode escape at offset %d", escape_pos);
			return -1;
		}

		if (code >= 0xd800 && code <= 0xdbff) {
			ch = input();
			if (ch != '\\') {
				json_set_error("expected low surrogate escape at offset %d", input_pos - 1);
				return -1;
			}
			ch = input();
			if (ch != 'u') {
				json_set_error("expected low surrogate escape at offset %d", input_pos - 1);
				return -1;
			}
			if (json_parse_hex4(&low)) {
				json_set_error("invalid low surrogate escape at offset %d", input_pos);
				return -1;
			}
			if (low < 0xdc00 || low > 0xdfff) {
				json_set_error("invalid low surrogate value at offset %d", input_pos);
				return -1;
			}

			code = 0x10000 + (((code - 0xd800) << 10) | (low - 0xdc00));
		} else if (code >= 0xdc00 && code <= 0xdfff) {
			json_set_error("unexpected low surrogate at offset %d", escape_pos);
			return -1;
		}

		if (json_buf_append_utf8(buf, len, cap, code)) {
			json_set_error("out of memory while decoding unicode escape");
			return -1;
		}
		return 0;
	default:
		json_set_error("invalid escape '\\%c' at offset %d", ch, escape_pos);
		return -1;
	}
}

static int json_enter_nesting(int offset)
{
	nesting_depth++;
	if (nesting_depth > JSON_MAX_DEPTH) {
		nesting_depth--;
		json_set_error("maximum nesting depth %d exceeded at offset %d",
			       JSON_MAX_DEPTH, offset);
		return -1;
	}

	return 0;
}

static void json_leave_nesting(void)
{
	if (nesting_depth > 0)
		nesting_depth--;
}

static int input(void)
{
	if (input_pos >= input_len) {
		input_can_unput = false;
		return JSON_INPUT_EOF;
	}

	input_can_unput = true;
	return (unsigned char)input_buf[input_pos++];
}

static void unput(void)
{
	if (input_can_unput && input_pos > 0) {
		input_pos--;
		input_can_unput = false;
	}
}

static void json_skip_whitespace(void)
{
	int ch;

	while ((ch = input()) != JSON_INPUT_EOF && isspace((unsigned char)ch))
		;
	unput();
}

static int json_parse_string(struct json_value *value)
{
	size_t len = 0;
	size_t cap = 0;
	char *str;
	int string_start;
	int ch;

	ch = input();
	if (ch != '"') {
		unput();
		return 0;
	}

	string_start = input_pos - 1;
	str = NULL;

	while ((ch = input()) != JSON_INPUT_EOF) {
		if (ch == '"')
			break;
		if (ch == '\\') {
			if (json_parse_escape(&str, &len, &cap, input_pos - 1)) {
				free(str);
				return -1;
			}
			continue;
		}
		if (ch < 0x20) {
			json_set_error("unescaped control character in string at offset %d", input_pos - 1);
			free(str);
			return -1;
		}
		if (json_buf_append(&str, &len, &cap, (unsigned char)ch)) {
			json_set_error("out of memory while parsing string");
			free(str);
			return -1;
		}
	}

	if (ch == JSON_INPUT_EOF) {
		json_set_error("unterminated string starting at offset %d", string_start);
		free(str);
		return -1;
	}

	if (json_buf_append(&str, &len, &cap, '\0')) {
		json_set_error("out of memory while finalizing string");
		free(str);
		return -1;
	}

	value->type = JSON_TYPE_STRING;
	value->u.string = str;

	return 1;
}

static int json_parse_number(struct json_value *value)
{
	char *token;
	char *endptr;
	double parsed;
	size_t len;
	int start;
	int end;
	int ch;

	ch = input();
	if (ch != '-' && (ch == JSON_INPUT_EOF || !isdigit(ch))) {
		unput();
		return 0;
	}

	start = input_pos - 1;

	if (ch == '-') {
		ch = input();
		if (ch == JSON_INPUT_EOF || !isdigit(ch)) {
			json_set_error("invalid number: '-' not followed by digit at offset %d", start);
			return -1;
		}
	}

	if (ch == '0') {
		ch = input();
		if (ch != JSON_INPUT_EOF && isdigit(ch)) {
			json_set_error("invalid number: leading zero at offset %d", start);
			return -1;
		}
	} else if (isdigit(ch)) {
		do {
			ch = input();
		} while (ch != JSON_INPUT_EOF && isdigit(ch));
	} else {
		json_set_error("invalid number at offset %d", start);
		return -1;
	}

	if (ch == '.') {
		ch = input();
		if (ch == JSON_INPUT_EOF || !isdigit(ch)) {
			json_set_error("invalid number: fractional part missing digits at offset %d",
				       start);
			return -1;
		}
		do {
			ch = input();
		} while (ch != JSON_INPUT_EOF && isdigit(ch));
	}

	if (ch == 'e' || ch == 'E') {
		ch = input();
		if (ch == '+' || ch == '-')
			ch = input();
		if (ch == JSON_INPUT_EOF || !isdigit(ch)) {
			json_set_error("invalid number: exponent missing digits at offset %d",
				       start);
			return -1;
		}
		do {
			ch = input();
		} while (ch != JSON_INPUT_EOF && isdigit(ch));
	}

	if (ch != JSON_INPUT_EOF) {
		end = input_pos - 1;
		unput();
	} else {
		end = input_pos;
	}

	len = (size_t)(end - start);
	token = malloc(len + 1);
	if (!token) {
		json_set_error("out of memory while parsing number");
		return -1;
	}

	memcpy(token, input_buf + start, len);
	token[len] = '\0';

	errno = 0;
	parsed = strtod(token, &endptr);
	if (endptr != token + len || errno == ERANGE || !isfinite(parsed)) {
		json_set_error("invalid or out-of-range number at offset %d", start);
		free(token);
		return -1;
	}
	free(token);

	value->type = JSON_TYPE_NUMBER;
	value->u.number = parsed;

	return 1;
}

static int json_parse_keyword(struct json_value *value)
{
	const char *match;
	int i;
	int ch;

	ch = input();
	switch (ch) {
	case 't':
		match = "true";
		value->type = JSON_TYPE_TRUE;
		break;
	case 'f':
		match = "false";
		value->type = JSON_TYPE_FALSE;
		break;
	case 'n':
		match = "null";
		value->type = JSON_TYPE_NULL;
		break;
	default:
		unput();
		return 0;
	}

	for (i = 1; match[i]; i++) {
		ch = input();
		if (ch == JSON_INPUT_EOF || ch != match[i]) {
			json_set_error("invalid keyword at offset %d", input_pos - 1);
			return -1;
		}
	}

	return 1;
}

static int json_parse_value(struct json_value *value)
{
	int ret;

	json_skip_whitespace();

	ret = json_parse_object(value);
	if (ret)
		goto out;

	ret = json_parse_array(value);
	if (ret)
		goto out;

	ret = json_parse_string(value);
	if (ret)
		goto out;

	ret = json_parse_number(value);
	if (ret)
		goto out;

	ret = json_parse_keyword(value);
	if (ret)
		goto out;

	json_set_error("unable to match value at offset %d", input_pos);
	return -1;

out:
	json_skip_whitespace();
	return ret;
}

static int json_parse_array(struct json_value *array)
{
	struct json_value *value;
	struct json_value *last = NULL;
	int ret;
	int ch;

	ch = input();
	if (ch != '[') {
		unput();
		return 0;
	}
	if (json_enter_nesting(input_pos - 1))
		return -1;

	array->type = JSON_TYPE_ARRAY;
	json_skip_whitespace();
	ch = input();
	if (ch == ']') {
		json_leave_nesting();
		return 1;
	}
	unput();

	do {
		value = calloc(1, sizeof(*value));
		if (!value) {
			json_set_error("out of memory while parsing array at offset %d", input_pos);
			json_leave_nesting();
			return -1;
		}

		ret = json_parse_value(value);
		if (ret <= 0) {
			json_set_error("invalid array element at offset %d", input_pos);
			free(value);
			json_leave_nesting();
			return -1;
		}

		if (!array->u.value)
			array->u.value = value;
		if (last)
			last->next = value;
		last = value;

		ch = input();
		if (ch == ']') {
			json_leave_nesting();
			return 1;
		}

	} while (ch == ',');

	json_set_error("expected ',' or ']' at offset %d", input_pos - 1);
	json_leave_nesting();

	return -1;
}

static int json_parse_object(struct json_value *object)
{
	struct json_value *value;
	struct json_value *last = NULL;
	int ret;
	int ch;

	ch = input();
	if (ch != '{') {
		unput();
		return 0;
	}
	if (json_enter_nesting(input_pos - 1))
		return -1;

	object->type = JSON_TYPE_OBJECT;
	json_skip_whitespace();
	ch = input();
	if (ch == '}') {
		json_leave_nesting();
		return 1;
	}
	unput();

	do {
		value = calloc(1, sizeof(*value));
		if (!value) {
			json_set_error("out of memory while parsing object at offset %d", input_pos);
			json_leave_nesting();
			return -1;
		}

		ret = json_parse_property(value);
		if (ret <= 0) {
			json_set_error("invalid object property at offset %d", input_pos);
			free(value);
			json_leave_nesting();
			return -1;
		}

		if (!object->u.value)
			object->u.value = value;
		if (last)
			last->next = value;
		last = value;

		ch = input();
		if (ch == '}') {
			json_leave_nesting();
			return 1;
		}
	} while (ch == ',');

	json_set_error("expected ',' or '}' at offset %d", input_pos - 1);
	json_leave_nesting();

	return -1;
}

static int json_parse_property(struct json_value *value)
{
	struct json_value key;
	int ret;
	int ch;

	json_skip_whitespace();

	ret = json_parse_string(&key);
	if (ret <= 0) {
		json_set_error("expected string key at offset %d", input_pos);
		return -1;
	}

	value->key = key.u.string;

	json_skip_whitespace();

	ch = input();
	if (ch != ':') {
		json_set_error("expected ':' after key at offset %d", input_pos - 1);
		return -1;
	}

	ret = json_parse_value(value);
	if (ret <= 0) {
		json_set_error("invalid property value at offset %d", input_pos);
		return -1;
	}

	return 1;
}

static struct json_value *json_parse_internal(const char *json, size_t len)
{
	struct json_value *root;
	int ret;

	input_buf = json;
	input_pos = 0;
	input_len = len;
	input_can_unput = false;
	nesting_depth = 0;
	json_error[0] = '\0';

	root = calloc(1, sizeof(*root));
	if (!root) {
		json_set_error("out of memory while allocating parse root");
		return NULL;
	}

	ret = json_parse_value(root);
	if (ret != 1) {
		json_set_error("parse error near offset %d", input_pos);
		json_free(root);
		return NULL;
	}

	json_skip_whitespace();
	if (input() != JSON_INPUT_EOF) {
		json_set_error("unexpected trailing token at offset %d", input_pos - 1);
		json_free(root);
		return NULL;
	}

	return root;
}

struct json_value *json_parse_buf(const char *json, size_t len)
{
	return json_parse_internal(json, len);
}

struct json_value *json_get_child(struct json_value *object, const char *key)
{
	struct json_value *it;

	if (!object || object->type != JSON_TYPE_OBJECT)
		return NULL;

	for (it = object->u.value; it; it = it->next) {
		if (!strcmp(it->key, key))
			return it;
	}

	return NULL;
}

struct json_value *json_get_element_object(struct json_value *object, unsigned int idx)
{
	struct json_value *it;
	unsigned int i;

	if (!object || object->type != JSON_TYPE_ARRAY)
		return NULL;

	for (it = object->u.value, i = 0; it; it = it->next, i++) {
		if (i == idx)
			return it;
	}

	return NULL;
}

const char *json_get_element_string(struct json_value *object, unsigned int idx)
{
	struct json_value *element;

	if (!object)
		return NULL;

	element = json_get_element_object(object, idx);
	if (!element || element->type != JSON_TYPE_STRING)
		return NULL;

	return element->u.string;
}

int json_count_children(struct json_value *array)
{
	struct json_value *it;
	int count = 0;

	if (!array || array->type != JSON_TYPE_ARRAY)
		return -1;

	for (it = array->u.value; it; it = it->next)
		count++;

	return count;
}

int json_get_number(struct json_value *object, const char *key, double *number)
{
	struct json_value *it;

	if (!object || object->type != JSON_TYPE_OBJECT)
		return -1;

	for (it = object->u.value; it; it = it->next) {
		if (!strcmp(it->key, key)) {
			if (it->type != JSON_TYPE_NUMBER)
				return -1;

			*number = it->u.number;
			return 0;
		}
	}

	return -1;
}

const char *json_get_string(struct json_value *object, const char *key)
{
	struct json_value *it;

	if (!object || object->type != JSON_TYPE_OBJECT)
		return NULL;

	for (it = object->u.value; it; it = it->next) {
		if (!strcmp(it->key, key)) {
			if (it->type != JSON_TYPE_STRING)
				return NULL;

			return it->u.string;
		}
	}

	return NULL;
}

void json_free(struct json_value *value)
{
	struct json_value *stack;
	struct json_value *next;
	struct json_value *it;

	if (!value)
		return;

	/* Free iteratively to avoid recursion depth issues on deeply nested JSON. */
	stack = value;
	stack->next = NULL;
	while (stack) {
		value = stack;
		stack = stack->next;

		if (value->type == JSON_TYPE_OBJECT || value->type == JSON_TYPE_ARRAY) {
			it = value->u.value;
			while (it) {
				next = it->next;
				it->next = stack;
				stack = it;
				it = next;
			}
		}

		free((char *)value->key);
		if (value->type == JSON_TYPE_STRING)
			free((char *)value->u.string);

		free(value);
	}
}
