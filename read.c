/*
 * Copyright (c) 2016-2017, Linaro Ltd.
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
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "read.h"
#include "qdl.h"

static struct read_op *read_ops;
static struct read_op *read_ops_last;

int read_op_load(const char *read_op_file)
{
	struct read_op *read_op;
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;
	int errors;

	doc = xmlReadFile(read_op_file, NULL, 0);
	if (!doc) {
		fprintf(stderr, "[READ] failed to parse %s\n", read_op_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	for (node = root->children; node ; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name, (xmlChar*)"read")) {
			fprintf(stderr, "[READ] unrecognized tag \"%s\", ignoring\n", node->name);
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
			fprintf(stderr, "[READ] errors while parsing read\n");
			free(read_op);
			continue;
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

int read_op_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct read_op *read_op, int fd),
				    const char *incdir)
{
	struct read_op *read_op;
	const char *filename;
	char tmp[PATH_MAX];
	int ret;
	int fd;

	for (read_op = read_ops; read_op; read_op = read_op->next) {
		filename = read_op->filename;
		if (incdir) {
			snprintf(tmp, PATH_MAX, "%s/%s", incdir, filename);
			if (access(tmp, F_OK) != -1)
				filename = tmp;
		}

		fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0644);

		if (fd < 0) {
			printf("Unable to open %s...\n", read_op->filename);
			return ret;
		}

		ret = apply(qdl, read_op, fd);

		close(fd);
		if (ret)
			return ret;
	}

	return 0;
}
