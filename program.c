// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * All rights reserved.
 */
#include <assert.h>
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "program.h"
#include "qdl.h"
#include "oscompat.h"
#include "firehose.h"
#include "sparse.h"
#include "gpt.h"

static int load_erase_tag(struct list_head *ops, xmlNode *node, bool is_nand)
{
	struct firehose_op *program;
	int errors = 0;

	program = firehose_alloc_op(FIREHOSE_OP_ERASE);
	if (!program)
		return -1;

	program->is_nand = is_nand;

	program->sector_size = attr_as_unsigned(node, "SECTOR_SIZE_IN_BYTES", &errors);
	program->num_sectors = attr_as_unsigned(node, "num_partition_sectors", &errors);
	program->partition = attr_as_unsigned(node, "physical_partition_number", &errors);
	program->start_sector = attr_as_string(node, "start_sector", &errors);
	if (is_nand) {
		program->pages_per_block = attr_as_unsigned(node, "PAGES_PER_BLOCK", &errors);
	}

	if (errors) {
		ux_err("errors while parsing erase tag\n");
		free(program);
		return -EINVAL;
	}

	list_append(ops, &program->node);

	return 0;
}

static int program_load_sparse(struct list_head *ops, struct firehose_op *program, int fd)
{
	struct firehose_op *program_sparse = NULL;
	char tmp[PATH_MAX];

	sparse_header_t sparse_header;
	unsigned int start_sector;
	uint32_t sparse_fill_value;
	uint64_t chunk_size;
	off_t sparse_offset;
	int chunk_type;

	if (sparse_header_parse(fd, &sparse_header)) {
		/*
		 * If the XML tag "program" contains the attribute 'sparse="true"'
		 * for a partition node but lacks a sparse header,
		 * it will be validated against the defined partition size.
		 * If the sizes match, it is likely that the 'sparse="true"' attribute
		 * was set by mistake, fix the sparse flag and add the
		 * program entry to the list.
		 */
		if ((off_t)program->sector_size * program->num_sectors ==
		    lseek(fd, 0, SEEK_END)) {
			program->sparse = false;
			list_append(ops, &program->node);
			return 0;
		}

		ux_err("[PROGRAM] Unable to parse sparse header at %s...failed\n",
		       program->filename);
		return -1;
	}

	for (uint32_t i = 0; i < sparse_header.total_chunks; ++i) {
		chunk_type = sparse_chunk_header_parse(fd, &sparse_header,
						       &chunk_size,
						       &sparse_fill_value,
						       &sparse_offset);
		if (chunk_type < 0) {
			ux_err("[PROGRAM] Unable to parse sparse chunk %i at %s...failed\n",
			       i, program->filename);
			return -1;
		}

		if (chunk_size == 0)
			continue;

		if (chunk_size % program->sector_size != 0) {
			ux_err("[SPARSE] File chunk #%u size %" PRIu64 " is not a sector-multiple\n",
			       i, chunk_size);
			return -1;
		}

		if (chunk_size / program->sector_size >= UINT_MAX) {
			/*
			 * Perhaps the programmer can handle larger "num_sectors"?
			 * Let's cap it for now, it's big enough for now...
			 */
			ux_err("[SPARSE] File chunk #%u size %" PRIu64 " is too large\n",
			       i, chunk_size);
			return -1;
		}

		if (chunk_type == CHUNK_TYPE_RAW || chunk_type == CHUNK_TYPE_FILL) {
			program_sparse = firehose_alloc_op(FIREHOSE_OP_PROGRAM);

			program_sparse->pages_per_block = program->pages_per_block;
			program_sparse->sector_size = program->sector_size;
			program_sparse->file_offset = program->file_offset;
			program_sparse->filename = strdup(program->filename);
			program_sparse->label = strdup(program->label);
			program_sparse->partition = program->partition;
			program_sparse->sparse = program->sparse;
			program_sparse->start_sector = strdup(program->start_sector);
			program_sparse->last_sector = program->last_sector;
			program_sparse->is_nand = program->is_nand;

			program_sparse->sparse_chunk_type = chunk_type;
			program_sparse->num_sectors = chunk_size / program->sector_size;

			if (chunk_type == CHUNK_TYPE_RAW)
				program_sparse->sparse_offset = sparse_offset;
			else
				program_sparse->sparse_fill_value = sparse_fill_value;

			list_append(ops, &program_sparse->node);
		}

		start_sector = (unsigned int)strtoul(program->start_sector, NULL, 0);
		start_sector += chunk_size / program->sector_size;
		sprintf(tmp, "%u", start_sector);
		free((void *)program->start_sector);
		program->start_sector = strdup(tmp);
	}

	return 0;
}

static int load_program_tag(struct list_head *ops, xmlNode *node, bool is_nand, bool allow_missing, const char *incdir)
{
	struct firehose_op *program;
	char tmp[PATH_MAX];
	int errors = 0;
	int ret;
	int fd = -1;

	program = firehose_alloc_op(FIREHOSE_OP_PROGRAM);
	if (!program)
		return -1;

	program->is_nand = is_nand;

	program->sector_size = attr_as_unsigned(node, "SECTOR_SIZE_IN_BYTES", &errors);
	program->filename = attr_as_string(node, "filename", &errors);
	program->label = attr_as_string(node, "label", &errors);
	program->num_sectors = attr_as_unsigned(node, "num_partition_sectors", &errors);
	program->partition = attr_as_unsigned(node, "physical_partition_number", &errors);
	program->sparse = attr_as_bool(node, "sparse", &errors);
	program->start_sector = attr_as_string(node, "start_sector", &errors);

	if (is_nand) {
		program->pages_per_block = attr_as_unsigned(node, "PAGES_PER_BLOCK", &errors);
		if (xmlGetProp(node, (xmlChar *)"last_sector")) {
			program->last_sector = attr_as_unsigned(node, "last_sector", &errors);
		}
	} else {
		program->file_offset = attr_as_unsigned(node, "file_sector_offset", &errors);
	}

	if (errors) {
		ux_err("errors while parsing program tag\n");
		free(program);
		return -EINVAL;
	}

	if (program->filename) {
		if (incdir) {
			snprintf(tmp, PATH_MAX, "%s/%s", incdir, program->filename);
			if (access(tmp, F_OK) != -1) {
				free((void *)program->filename);
				program->filename = strdup(tmp);
			}
		}

		fd = open(program->filename, O_RDONLY | O_BINARY);
		if (fd < 0) {
			ux_info("unable to open %s", program->filename);
			if (!allow_missing) {
				ux_info("...failing\n");
				return -1;
			}
			ux_info("...ignoring\n");

			free((void *)program->filename);
			program->filename = NULL;
		}
	}

	if (fd >= 0 && program->sparse) {
		ret = program_load_sparse(ops, program, fd);
		if (ret < 0)
			return -1;

		/*
		 * Chunks were added to the program list, drop the filename of
		 * the parent, to prevent this from being written to the device
		 */
		free((void *)program->filename);
		program->filename = NULL;
	}

	list_append(ops, &program->node);

	if (fd >= 0)
		close(fd);

	return 0;
}

int program_load(struct list_head *ops, const char *program_file, bool is_nand, bool allow_missing, const char *incdir)
{
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;
	int errors = 0;

	doc = xmlReadFile(program_file, NULL, 0);
	if (!doc) {
		ux_err("failed to parse program-type file \"%s\"\n", program_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	for (node = root->children; node ; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (!xmlStrcmp(node->name, (xmlChar *)"erase"))
			errors = load_erase_tag(ops, node, is_nand);
		else if (!xmlStrcmp(node->name, (xmlChar *)"program"))
			errors = load_program_tag(ops, node, is_nand, allow_missing, incdir);
		else {
			ux_err("unrecognized tag \"%s\" in program-type file \"%s\"\n", node->name, program_file);
			errors = -EINVAL;
		}

		if (errors)
			goto out;
	}

out:
	xmlFreeDoc(doc);

	return errors;
}

int erase_execute(struct qdl_device *qdl, struct firehose_op *op,
		  int (*apply)(struct qdl_device *qdl, struct firehose_op *op))
{
	int ret;

	assert(op->type == FIREHOSE_OP_ERASE);

	ret = apply(qdl, op);
	if (ret)
		return ret;

	return 0;
}

static struct firehose_op *program_find_partition(struct list_head *ops, const char *partition)
{
	struct firehose_op *program;
	const char *label;

	list_for_each_entry(program, ops, node) {
		if (program->type != FIREHOSE_OP_PROGRAM)
			continue;

		label = program->label;
		if (!label)
			continue;

		if (!strcmp(label, partition))
			return program;
	}

	return NULL;
}

/**
 * program_find_bootable_partition() - find one bootable partition
 *
 * Returns partition number, or negative errno on failure.
 *
 * Scan program tags for a partition with the label "sbl1", "xbl" or "xbl_a"
 * and return the partition number for this. If more than one line matches
 * we're informing the caller so that they can warn the user about the
 * uncertainty of this logic.
 */
int program_find_bootable_partition(struct list_head *ops, bool *multiple_found)
{
	struct firehose_op *program;
	int part = -ENOENT;

	*multiple_found = false;

	program = program_find_partition(ops, "xbl");
	if (program)
		part = program->partition;

	program = program_find_partition(ops, "xbl_a");
	if (program) {
		if (part != -ENOENT)
			*multiple_found = true;
		else
			part = program->partition;
	}

	program = program_find_partition(ops, "sbl1");
	if (program) {
		if (part != -ENOENT)
			*multiple_found = true;
		else
			part = program->partition;
	}

	return part;
}

/**
 * program_is_sec_partition_flashed() - find if secdata partition is flashed
 *
 * Returns true if filename for secdata is set in program*.xml,
 * or false otherwise.
 */
int program_is_sec_partition_flashed(struct list_head *ops)
{
	struct firehose_op *program;

	program = program_find_partition(ops, "secdata");
	if (!program)
		return false;

	if (program->filename)
		return true;

	return false;
}

int program_cmd_add(struct list_head *ops, const char *address, const char *filename)
{
	unsigned int start_sector;
	unsigned int num_sectors;
	struct firehose_op *program;
	char *gpt_partition;
	int partition;
	char buf[20];
	int ret;

	ret = parse_storage_address(address, &partition, &start_sector, &num_sectors, &gpt_partition);
	if (ret < 0)
		return ret;

	program = firehose_alloc_op(FIREHOSE_OP_PROGRAM);
	if (!program) {
		ux_err("failed to allocate program command\n");
		return -1;
	}

	program->sector_size = 0;
	program->file_offset = 0;
	program->filename = filename ? strdup(filename) : NULL;
	program->label = filename ? strdup(filename) : NULL;
	program->num_sectors = num_sectors;
	program->partition = partition;
	program->sparse = false;
	sprintf(buf, "%u", start_sector);
	program->start_sector = strdup(buf);
	program->last_sector = 0;
	program->is_nand = false;
	program->gpt_partition = gpt_partition;

	list_append(ops, &program->node);

	return 0;
}

int erase_cmd_add(struct list_head *ops, const char *address)
{
	unsigned int start_sector;
	unsigned int num_sectors;
	struct firehose_op *program;
	char *gpt_partition;
	int partition;
	char buf[20];
	int ret;

	ret = parse_storage_address(address, &partition, &start_sector, &num_sectors, &gpt_partition);
	if (ret < 0)
		return ret;

	program = firehose_alloc_op(FIREHOSE_OP_ERASE);
	if (!program) {
		ux_err("failed to allocate erase command\n");
		return -1;
	}

	program->num_sectors = num_sectors;
	program->partition = partition;
	sprintf(buf, "%u", start_sector);
	program->start_sector = strdup(buf);
	program->gpt_partition = gpt_partition;

	list_append(ops, &program->node);

	return 0;
}

int program_resolve_gpt_deferrals(struct qdl_device *qdl, struct list_head *ops)
{
	unsigned int start_sector;
	struct firehose_op *op;
	char buf[20];
	int ret;

	list_for_each_entry(op, ops, node) {
		if (op->type != FIREHOSE_OP_PROGRAM)
			continue;
		if (!op->gpt_partition)
			continue;

		ret = gpt_find_by_name(qdl, op->gpt_partition, &op->partition,
				       &start_sector, &op->num_sectors);
		if (ret < 0)
			return -1;

		sprintf(buf, "%u", start_sector);
		op->start_sector = strdup(buf);
	}

	return 0;
}
