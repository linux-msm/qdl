// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * All rights reserved.
 */
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
#include "sparse.h"

static struct program *programes;
static struct program *programes_last;

static int load_erase_tag(xmlNode *node, bool is_nand)
{
	struct program *program;
	int errors = 0;

	program = calloc(1, sizeof(struct program));

	program->is_nand = is_nand;
	program->is_erase = true;

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

	if (programes) {
		programes_last->next = program;
		programes_last = program;
	} else {
		programes = program;
		programes_last = program;
	}

	return 0;
}

static struct program *program_load_sparse(struct program *program, int fd)
{
	struct program *program_sparse = NULL;
	struct program *programes_sparse = NULL;
	struct program *programes_sparse_last = NULL;
	char tmp[PATH_MAX];

	sparse_header_t sparse_header;
	unsigned int start_sector, chunk_type;
	uint32_t sparse_fill_value;
	uint64_t chunk_size;
	off_t sparse_offset;

	if (sparse_header_parse(fd, &sparse_header)) {
		/*
		 * If the XML tag "program" contains the attribute 'sparse="true"'
		 * for a partition node but lacks a sparse header,
		 * it will be validated against the defined partition size.
		 * If the sizes match, it is likely that the 'sparse="true"' attribute
		 * was set by mistake.
		 */
		if ((off_t)program->sector_size * program->num_sectors ==
		    lseek(fd, 0, SEEK_END)) {
			program_sparse = calloc(1, sizeof(struct program));
			memcpy(program_sparse, program, sizeof(struct program));
			program_sparse->sparse = false;
			program_sparse->next = NULL;
			return program_sparse;
		}

		ux_err("[PROGRAM] Unable to parse sparse header at %s...failed\n",
		       program->filename);
		return NULL;
	}

	for (uint32_t i = 0; i < sparse_header.total_chunks; ++i) {
		chunk_type = sparse_chunk_header_parse(fd, &sparse_header,
						       &chunk_size,
						       &sparse_fill_value,
						       &sparse_offset);

		switch (chunk_type) {
		case CHUNK_TYPE_RAW:
		case CHUNK_TYPE_FILL:
		case CHUNK_TYPE_DONT_CARE:
			break;
		default:
			ux_err("[PROGRAM] Unable to parse sparse chunk %i at %s...failed\n",
			       i, program->filename);
			return NULL;
		}

		if (chunk_size == 0)
			continue;

		if (chunk_size % program->sector_size != 0) {
			ux_err("[SPARSE] File chunk #%u size %" PRIu64 " is not a sector-multiple\n",
			       i, chunk_size);
			return NULL;
		}

		if (chunk_size / program->sector_size >= UINT_MAX) {
			/*
			 * Perhaps the programmer can handle larger "num_sectors"?
			 * Let's cap it for now, it's big enough for now...
			 */
			ux_err("[SPARSE] File chunk #%u size %" PRIu64 " is too large\n",
			       i, chunk_size);
			return NULL;
		}

		switch (chunk_type) {
		case CHUNK_TYPE_RAW:
			program_sparse = calloc(1, sizeof(struct program));
			memcpy(program_sparse, program, sizeof(struct program));

			program_sparse->next = NULL;
			program_sparse->num_sectors = chunk_size / program->sector_size;

			program_sparse->sparse_chunk_type = CHUNK_TYPE_RAW;
			program_sparse->sparse_offset = sparse_offset;

			if (programes_sparse) {
				programes_sparse_last->next = program_sparse;
				programes_sparse_last = program_sparse;
			} else {
				programes_sparse = program_sparse;
				programes_sparse_last = program_sparse;
			}

			break;
		case CHUNK_TYPE_FILL:

			program_sparse = calloc(1, sizeof(struct program));
			memcpy(program_sparse, program, sizeof(struct program));

			program_sparse->next = NULL;
			program_sparse->num_sectors = chunk_size / program->sector_size;

			program_sparse->sparse_chunk_type = CHUNK_TYPE_FILL;
			program_sparse->sparse_fill_value = sparse_fill_value;

			if (programes_sparse) {
				programes_sparse_last->next = program_sparse;
				programes_sparse_last = program_sparse;
			} else {
				programes_sparse = program_sparse;
				programes_sparse_last = program_sparse;
			}

			break;
		default:
			break;
		}

		start_sector = (unsigned int)strtoul(program->start_sector, NULL, 0);
		start_sector += chunk_size / program->sector_size;
		sprintf(tmp, "%u", start_sector);
		program->start_sector = strdup(tmp);
	}

	return programes_sparse;
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
			errors = load_erase_tag(node, is_nand);
		else if (!xmlStrcmp(node->name, (xmlChar *)"program"))
			errors = load_program_tag(node, is_nand);
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

int program_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program, int fd),
		    const char *incdir, bool allow_missing)
{
	struct program *program;
	struct program *program_sparse;
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

		fd = open(filename, O_RDONLY | O_BINARY);

		if (fd < 0) {
			ux_info("unable to open %s", program->filename);
			if (!allow_missing) {
				ux_info("...failing\n");
				return -1;
			}
			ux_info("...ignoring\n");
			continue;
		}

		if (!program->sparse) {
			ret = apply(qdl, program, fd);
		} else {
			program_sparse = program_load_sparse(program, fd);
			if (!program_sparse) {
				ux_err("[PROGRAM] load sparse failed\n");
				return -EINVAL;
			}

			for (; program_sparse; program_sparse = program_sparse->next) {
				ret = apply(qdl, program_sparse, fd);
				if (ret)
					break;
			}
		}

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

static struct program *program_find_partition(const char *partition)
{
	struct program *program;
	const char *label;

	for (program = programes; program; program = program->next) {
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
int program_find_bootable_partition(bool *multiple_found)
{
	struct program *program;
	int part = -ENOENT;

	*multiple_found = false;

	program = program_find_partition("xbl");
	if (program)
		part = program->partition;

	program = program_find_partition("xbl_a");
	if (program) {
		if (part != -ENOENT)
			*multiple_found = true;
		else
			part = program->partition;
	}

	program = program_find_partition("sbl1");
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
int program_is_sec_partition_flashed(void)
{
	struct program *program;

	program = program_find_partition("secdata");
	if (!program)
		return false;

	if (program->filename)
		return true;

	return false;
}

void free_programs(void)
{
	struct program *program = programes;
	struct program *next;

	for (program = programes; program; program = next) {
		next = program->next;
		free((void *)program->filename);
		free((void *)program->label);
		free((void *)program->start_sector);
		free(program);
	}

	programes = NULL;
}
