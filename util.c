// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016, Bjorn Andersson <bjorn@kryo.se>
 * All rights reserved.
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "version.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))

static uint8_t to_hex(uint8_t ch)
{
	ch &= 0xf;
	return ch <= 9 ? '0' + ch : 'a' + ch - 10;
}

void print_version(void)
{
	extern const char *__progname;
	fprintf(stdout, "%s version %s\n", __progname, VERSION);
}

void print_hex_dump(const char *prefix, const void *buf, size_t len)
{
	const uint8_t *ptr = buf;
	size_t linelen;
	uint8_t ch;
	char line[16 * 3 + 16 + 1];
	int li;
	int i;
	int j;

	for (i = 0; i < len; i += 16) {
		linelen = MIN(16, len - i);
		li = 0;

		for (j = 0; j < linelen; j++) {
			ch = ptr[i + j];
			line[li++] = to_hex(ch >> 4);
			line[li++] = to_hex(ch);
			line[li++] = ' ';
		}

		for (; j < 16; j++) {
			line[li++] = ' ';
			line[li++] = ' ';
			line[li++] = ' ';
		}

		for (j = 0; j < linelen; j++) {
			ch = ptr[i + j];
			line[li++] = isprint(ch) ? ch : '.';
		}

		line[li] = '\0';

		printf("%s %04x: %s\n", prefix, i, line);
	}
}

unsigned attr_as_unsigned(xmlNode *node, const char *attr, int *errors)
{
	unsigned int ret;
	xmlChar *value;

	value = xmlGetProp(node, (xmlChar*)attr);
	if (!value) {
		(*errors)++;
		return 0;
	}

	ret = (unsigned int) strtoul((char*)value, NULL, 0);
	xmlFree(value);
	return ret;
}

const char *attr_as_string(xmlNode *node, const char *attr, int *errors)
{
	xmlChar *value;
	char *ret = NULL;

	value = xmlGetProp(node, (xmlChar*)attr);
	if (!value) {
		(*errors)++;
		return NULL;
	}

	if (value[0] != '\0')
		ret = strdup((char*)value);

	xmlFree(value);
	return ret;
}
