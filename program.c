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
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "program.h"
#include "qdl.h"

static struct program *programes;
static struct program *programes_last;

static int load_erase_tag(xmlNode *node, bool is_nand)
{
	struct program *program;
	int errors = 0;

	if (!is_nand) {
		fprintf(stderr, "got \"erase\" tag for non-NAND storage\n");
		return -EINVAL;
	}

	program = calloc(1, sizeof(struct program));


	program->is_nand = true;
	program->is_erase = true;

	program->pages_per_block = attr_as_unsigned(node, "PAGES_PER_BLOCK", &errors);
	program->sector_size = attr_as_unsigned(node, "SECTOR_SIZE_IN_BYTES", &errors);
	program->num_sectors = attr_as_unsigned(node, "num_partition_sectors", &errors);
	program->start_sector = attr_as_string(node, "start_sector", &errors);

	if (errors) {
		fprintf(stderr, "[PROGRAM] errors while parsing erase tag\n");
		free(program);
		return -EINVAL;
	}

	if (programes) {
		programes_last->next = program;
		programes_last = program;
	} else {
		programes = program;
		programes_last = program;
	}

	return 0;
}

static int load_program_tag(xmlNode *node, bool is_nand)
{
	struct program *program;
	int errors = 0;

	program = calloc(1, sizeof(struct program));

	program->is_nand = is_nand;

	program->sector_size = attr_as_unsigned(node, "SECTOR_SIZE_IN_BYTES", &errors);
	program->filename = attr_as_string(node, "filename", &errors);
	program->label = attr_as_string(node, "label", &errors);
	program->num_sectors = attr_as_unsigned(node, "num_partition_sectors", &errors);
	program->partition = attr_as_unsigned(node, "physical_partition_number", &errors);
	program->start_sector = attr_as_string(node, "start_sector", &errors);

	if (is_nand) {
		program->pages_per_block = attr_as_unsigned(node, "PAGES_PER_BLOCK", &errors);
		if (NULL != xmlGetProp(node, (xmlChar *)"last_sector")) {
			program->last_sector = attr_as_unsigned(node, "last_sector", &errors);
		}
	} else {
		program->file_offset = attr_as_unsigned(node, "file_sector_offset", &errors);
	}

	if (errors) {
		fprintf(stderr, "[PROGRAM] errors while parsing program\n");
		free(program);
		return -EINVAL;
	}

	if (programes) {
		programes_last->next = program;
		programes_last = program;
	} else {
		programes = program;
		programes_last = program;
	}

	return 0;
}

int program_load(const char *program_file, bool is_nand)
{
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;
	int errors;

	doc = xmlReadFile(program_file, NULL, 0);
	if (!doc) {
		fprintf(stderr, "[PROGRAM] failed to parse %s\n", program_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	for (node = root->children; node ; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		errors = -EINVAL;

		if (!xmlStrcmp(node->name, (xmlChar *)"erase"))
			errors = load_erase_tag(node, is_nand);
		else if (!xmlStrcmp(node->name, (xmlChar *)"program"))
			errors = load_program_tag(node, is_nand);
		else
			fprintf(stderr, "[PROGRAM] unrecognized tag \"%s\", ignoring\n", node->name);

		if (errors)
			return errors;
	}

	xmlFreeDoc(doc);

	return 0;
}

int program_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program, int fd),
		    const char *incdir)
{
	struct program *program;
	const char *filename;
	char tmp[PATH_MAX];
	int ret;
	int fd;

	for (program = programes; program; program = program->next) {
		if (program->is_erase || !program->filename)
			continue;

		filename = program->filename;
		if (incdir) {
			snprintf(tmp, PATH_MAX, "%s/%s", incdir, filename);
			if (access(tmp, F_OK) != -1)
				filename = tmp;
		}

		fd = open(filename, O_RDONLY);

		if (fd < 0) {
			printf("Unable to open %s...ignoring\n", program->filename);
			continue;
		}

		ret = apply(qdl, program, fd);

		close(fd);
		if (ret)
			return ret;
	}

	return 0;
}

int erase_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program))
{
	struct program *program;
	int ret;


	for (program = programes; program; program = program->next) {
		if (!program->is_erase)
			continue;

		ret = apply(qdl, program);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * program_find_bootable_partition() - find one bootable partition
 *
 * Returns partition number, or negative errno on failure.
 *
 * Scan program tags for a partition with the label "sbl1", "xbl" or "xbl_a"
 * and return the partition number for this. If more than one line matches
 * we're assuming our logic is flawed and return an error.
 */
int program_find_bootable_partition(void)
{
	struct program *program;
	const char *label;
	int part = -ENOENT;

	for (program = programes; program; program = program->next) {
		label = program->label;
		if (!label)
			continue;

		if (!strcmp(label, "xbl") || !strcmp(label, "xbl_a") ||
		    !strcmp(label, "sbl1")) {
			if (part != -ENOENT)
				return -EINVAL;

			part = program->partition;
		}
	}

	return part;
}
