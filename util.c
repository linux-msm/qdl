// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016, Bjorn Andersson <bjorn@kryo.se>
 * All rights reserved.
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <unistd.h>

#include "oscompat.h"
#include "qdl.h"
#include "version.h"

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
	unsigned int i;
	unsigned int j;

	for (i = 0; i < len; i += 16) {
		linelen = MIN(16u, (size_t)(len - i));
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

unsigned int attr_as_unsigned(xmlNode *node, const char *attr, int *errors)
{
	unsigned int ret;
	xmlChar *value;

	value = xmlGetProp(node, (xmlChar *)attr);
	if (!value) {
		(*errors)++;
		return 0;
	}

	ret = (unsigned int)strtoul((char *)value, NULL, 0);
	xmlFree(value);
	return ret;
}

const char *attr_as_string(xmlNode *node, const char *attr, int *errors)
{
	xmlChar *value;
	char *ret = NULL;

	value = xmlGetProp(node, (xmlChar *)attr);
	if (!value) {
		(*errors)++;
		return NULL;
	}

	if (value[0] != '\0')
		ret = strdup((char *)value);

	xmlFree(value);
	return ret;
}

bool attr_as_bool(xmlNode *node, const char *attr, int *errors)
{
	xmlChar *value;
	bool ret = false;

	if (!xmlHasProp(node, (xmlChar *)attr))
		return false;

	value = xmlGetProp(node, (xmlChar *)attr);
	if (!value) {
		(*errors)++;
		return false;
	}

	ret = (xmlStrcmp(value, (xmlChar *)"true") == 0);
	xmlFree(value);
	return ret;
}

/***
 * parse_storage_address() - parse a storage address specifier
 * @address: specifier to be parsed
 * @physical_partition: physical partition
 * @start_sector: start_sector
 * @num_sectors: number of sectors
 * @gpt_partition: GPT name
 *
 * This function parses the provided address specifier and detects the
 * following patterns:
 *
 * N => physical partition N, sector 0
 * N/S => physical partition N, sector S
 * N/S+L => physical partition N, L sectors at sector S
 * name => GPT partition name match across all physical partitions
 * N/name => GPT partition name match within physical partition N
 *
 * @physical_partition is either the requested physical partition, or -1 if
 * none is specified. Either @start_sector and @num_sectors, or @gpt_partition
 * will represent the equested address, the other(s) will be zeroed.
 *
 * Returns: 0 on success, -1 on failure
 */
int parse_storage_address(const char *address, int *physical_partition,
			  unsigned int *start_sector, unsigned int *num_sectors,
			  char **gpt_partition)
{
	unsigned long length = 0;
	const char *ptr = address;
	unsigned long sector = 0;
	long partition;
	char *end;
	char *gpt = NULL;

	errno = 0;
	partition = strtol(ptr, &end, 10);
	if (end == ptr) {
		partition = -1;
		gpt = strdup(ptr);
		goto done;
	}
	if ((errno == ERANGE && partition == LONG_MAX) || partition < 0)
		return -1;

	if (end[0] == '\0')
		goto done;
	if (end[0] != '/')
		return -1;

	ptr = end + 1;

	errno = 0;
	sector = strtoul(ptr, &end, 10);
	if (end == ptr) {
		gpt = strdup(ptr);
		goto done;
	}
	if (errno == ERANGE && sector == ULONG_MAX)
		return -1;

	if (end[0] == '\0')
		goto done;
	if (end[0] != '+')
		return -1;

	ptr = end + 1;

	errno = 0;
	length = strtoul(ptr, &end, 10);
	if (end == ptr)
		return -1;
	if (errno == ERANGE && length == ULONG_MAX)
		return -1;
	if (length == 0)
		return -1;

	if (end[0] != '\0')
		return -1;

done:
	*physical_partition = partition;
	*start_sector = sector;
	*num_sectors = length;
	*gpt_partition = gpt;

	return 0;
}

/**
 * load_sahara_image() - Load the content of the given file into the image
 * @filename: file to be loaded
 * @image: Sahara image object to be populated
 *
 * Read the content of the given @filename into the given @image, update the
 * @image->len, and then populate the @image->name for debugging purposes.
 *
 * Returns: 0 on success, -1 on error
 */
int load_sahara_image(const char *filename, struct sahara_image *image)
{
	ssize_t n;
	off_t len;
	void *ptr;
	int fd;

	fd = open(filename, O_RDONLY | O_BINARY);
	if (fd < 0) {
		ux_err("failed to read \"%s\"\n", filename);
		return -1;
	}

	len = lseek(fd, 0, SEEK_END);
	if (len < 0) {
		ux_err("failed to find end of \"%s\"\n", filename);
		goto err_close;
	}
	lseek(fd, 0, SEEK_SET);

	ptr = malloc(len);
	if (!ptr) {
		ux_err("failed to init buffer for content of \"%s\"\n", filename);
		goto err_close;
	}

	n = read(fd, ptr, len);
	if (n != len) {
		ux_err("failed to read content of \"%s\"\n", filename);
		free(ptr);
		goto err_close;
	}

	close(fd);

	image->name = strdup(filename);
	image->ptr = ptr;
	image->len = len;

	return 0;

err_close:
	close(fd);
	return -1;
}
