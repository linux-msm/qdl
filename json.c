/*
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
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "json.h"

static const char *input_buf;
static int input_pos;
static int input_len;

static int json_parse_array(struct json_value *array);
static int json_parse_object(struct json_value *object);
static int json_parse_property(struct json_value *value);

static int input(void)
{
	if (input_pos >= input_len)
		return 0;

	return input_buf[input_pos++];
}

static void unput(void)
{
	input_pos--;
}

static void json_skip_whitespace(void)
{
	int ch;

	while ((ch = input()) && isspace(ch))
		;
	unput();
}

static int json_parse_string(struct json_value *value)
{
	char buf[128];
	char *b = buf;
	int ch;

	ch = input();
	if (ch != '"') {
		unput();
		return 0;
	}

	while ((ch = input()) && ch != '"')
		*b++ = ch;
	*b = '\0';

	if (!ch)
		return -1;

	value->type = JSON_TYPE_STRING;
	value->u.string = strdup(buf);

	return 1;
}

static int json_parse_number(struct json_value *value)
{
	char buf[20];
	char *b = buf;
	int ch;

	while ((ch = input()) && isdigit(ch) && b - buf < sizeof(buf) - 1)
		*b++ = ch;
	*b = '\0';
	unput();

	if (b == buf)
		return 0;

	value->type = JSON_TYPE_NUMBER;
	value->u.number = strtod(buf, NULL);

	return 1;
}

static int json_parse_keyword(struct json_value *value)
{
	const char *match;
	const char *m;
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

	m = match;
	while (*m && *m++ == ch)
		ch = input();
	unput();

	return *m == '\0' ? 1 : -1;
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

	fprintf(stderr, "unable to match a value\n");
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

	array->type = JSON_TYPE_ARRAY;
	do {
		value = calloc(1, sizeof(*value));
		if (!value)
			return -1;

		ret = json_parse_value(value);
		if (ret <= 0) {
			free(value);
			return -1;
		}

		if (!array->u.value)
			array->u.value = value;
		if (last)
			last->next = value;
		last = value;

		ch = input();
		if (ch == ']') {
			return 1;
		}

	} while (ch == ',');

	fprintf(stderr, "expected ',' got '%c'\n", ch);

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

	object->type = JSON_TYPE_OBJECT;

	do {
		value = calloc(1, sizeof(*value));
		if (!value)
			return -1;

		ret = json_parse_property(value);
		if (ret <= 0) {
			free(value);
			return -1;
		}

		if (!object->u.value)
			object->u.value = value;
		if (last)
			last->next = value;
		last = value;

		ch = input();
		if (ch == '}') {
			return 1;
		}
	} while (ch == ',');

	return -1;
}

static int json_parse_property(struct json_value *value)
{
	struct json_value key;
	int ret;
	int ch;

	json_skip_whitespace();

	ret = json_parse_string(&key);
	if (ret <= 0)
		return -1;

	value->key = key.u.string;

	json_skip_whitespace();

	ch = input();
	if (ch != ':')
		return -1;

	ret = json_parse_value(value);
	if (ret <= 0)
		return -1;

	return 1;
}

struct json_value *json_parse(const char *json)
{
	struct json_value *root;
	int ret;

	input_buf = json;
	input_pos = 0;
	input_len = strlen(input_buf);

	root = calloc(1, sizeof(*root));
	if (!root)
		return NULL;

	ret = json_parse_value(root);
	if (ret != 1) {
		free(root);
		return NULL;
	}

	return root;
}

struct json_value *json_parse_file(const char *file)
{
	struct json_value *root;
	struct stat sb;
	int ret;
	int fd;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open %s: %s\n", file, strerror(errno));
		return NULL;
	}

	ret = fstat(fd, &sb);
	if (ret < 0)
		return NULL;

	input_pos = 0;
	input_len = sb.st_size;
	input_buf = malloc(sb.st_size);

	ret = read(fd, (char *)input_buf, input_len);

	close(fd);

	if (ret != input_len) {
		fprintf(stderr, "failed to read %d bytes form %s\n", input_len, file);
		return NULL;
	}

	root = calloc(1, sizeof(*root));
	if (!root)
		return NULL;

	ret = json_parse_value(root);
	if (ret != 1) {
		free(root);
		return NULL;
	}

	return root;
}

struct json_value *json_get_child(struct json_value *object, const char *key)
{
	struct json_value *it;

	if(object->type != JSON_TYPE_OBJECT)
		return NULL;

	for (it = object->u.value; it; it = it->next) {
		if (!strcmp(it->key, key))
			return it;
	}

	return NULL;
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
	struct json_value *next;
	struct json_value *it;

	free((char *)value->key);

	switch (value->type) {
	case JSON_TYPE_OBJECT:
	case JSON_TYPE_ARRAY:
		it = value->u.value;
		while (it) {
			next = it->next;
			json_free(it);
			it = next;
		}
		break;
	case JSON_TYPE_STRING:
		free((char *)value->u.string);
		break;
	}

	free(value);
}
