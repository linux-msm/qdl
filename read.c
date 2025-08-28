// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * All rights reserved.
 */
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "read.h"
#include "qdl.h"
#include "oscompat.h"
#include "gpt.h"

static struct read_op *read_ops;
static struct read_op *read_ops_last;

int read_op_load(const char *read_op_file, const char *incdir)
{
	struct read_op *read_op;
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;
	int errors;
	char tmp[PATH_MAX];

	doc = xmlReadFile(read_op_file, NULL, 0);
	if (!doc) {
		ux_err("failed to parse read-type file \"%s\"\n", read_op_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	for (node = root->children; node ; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name, (xmlChar *)"read")) {
			ux_err("unrecognized tag \"%s\" in read-type file \"%s\", ignoring\n",
			       node->name, read_op_file);
			continue;
		}

		errors = 0;

		read_op = calloc(1, sizeof(struct read_op));

		read_op->sector_size = attr_as_unsigned(node, "SECTOR_SIZE_IN_BYTES", &errors);
		read_op->filename = attr_as_string(node, "filename", &errors);
		read_op->partition = attr_as_unsigned(node, "physical_partition_number", &errors);
		read_op->num_sectors = attr_as_unsigned(node, "num_partition_sectors", &errors);
		read_op->start_sector = attr_as_string(node, "start_sector", &errors);

		if (errors) {
			ux_err("errors while parsing read-type file \"%s\"\n", read_op_file);
			free(read_op);
			continue;
		}

		if (incdir) {
			snprintf(tmp, PATH_MAX, "%s/%s", incdir, read_op->filename);
			if (access(tmp, F_OK) != -1)
				read_op->filename = strdup(tmp);
		}

		if (read_ops) {
			read_ops_last->next = read_op;
			read_ops_last = read_op;
		} else {
			read_ops = read_op;
			read_ops_last = read_op;
		}
	}

	xmlFreeDoc(doc);

	return 0;
}

int read_op_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct read_op *read_op, int fd))
{
	struct read_op *read_op;
	int ret;
	int fd;

	for (read_op = read_ops; read_op; read_op = read_op->next) {
		fd = open(read_op->filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
		if (fd < 0) {
			ux_info("unable to open %s...\n", read_op->filename);
			return ret;
		}

		ret = apply(qdl, read_op, fd);

		close(fd);
		if (ret)
			return ret;
	}

	return 0;
}

int read_cmd_add(const char *address, const char *filename)
{
	unsigned int start_sector;
	unsigned int num_sectors;
	struct read_op *read_op;
	char *gpt_partition;
	int partition;
	char buf[20];
	int ret;

	ret = parse_storage_address(address, &partition, &start_sector, &num_sectors, &gpt_partition);
	if (ret < 0)
		return ret;

	if (num_sectors == 0 && !gpt_partition) {
		ux_err("read command without length specifier not supported\n");
		return -1;
	}

	read_op = calloc(1, sizeof(struct read_op));

	read_op->sector_size = 0;
	read_op->filename = strdup(filename);
	read_op->partition = partition;
	read_op->num_sectors = num_sectors;
	sprintf(buf, "%u", start_sector);
	read_op->start_sector = strdup(buf);
	read_op->gpt_partition = gpt_partition;

	if (read_ops) {
		read_ops_last->next = read_op;
		read_ops_last = read_op;
	} else {
		read_ops = read_op;
		read_ops_last = read_op;
	}

	return 0;
}

int read_resolve_gpt_deferrals(struct qdl_device *qdl)
{
	struct read_op *read_op;
	unsigned int start_sector;
	char buf[20];
	int ret;

	for (read_op = read_ops; read_op; read_op = read_op->next) {
		if (!read_op->gpt_partition)
			continue;

		ret = gpt_find_by_name(qdl, read_op->gpt_partition, &read_op->partition,
				       &start_sector, &read_op->num_sectors);
		if (ret < 0)
			return -1;

		sprintf(buf, "%u", start_sector);
		read_op->start_sector = strdup(buf);
	}

	return 0;
}
