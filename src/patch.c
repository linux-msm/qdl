// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * All rights reserved.
 */
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <unistd.h>

#include "patch.h"
#include "firehose.h"
#include "qdl.h"

static bool patches_loaded;

int patch_load_xml(struct list_head *ops, xmlDoc *doc, const char *patch_file)
{
	struct firehose_op *patch;
	xmlNode *node;
	xmlNode *root;
	int errors;

	root = xmlDocGetRootElement(doc);
	for (node = root->children; node ; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name, (xmlChar *)"patch")) {
			ux_err("unrecognized tag \"%s\" in patch-type file, ignoring\n", node->name);
			continue;
		}

		errors = 0;

		patch = firehose_alloc_op(FIREHOSE_OP_PATCH);

		patch->sector_size = attr_as_unsigned(node, "SECTOR_SIZE_IN_BYTES", &errors);
		patch->byte_offset = attr_as_unsigned(node, "byte_offset", &errors);
		patch->filename = attr_as_string(node, "filename", &errors);
		patch->partition = attr_as_unsigned(node, "physical_partition_number", &errors);
		patch->size_in_bytes = attr_as_unsigned(node, "size_in_bytes", &errors);
		patch->start_sector = attr_as_string(node, "start_sector", &errors);
		patch->value = attr_as_string(node, "value", &errors);
		patch->what = attr_as_string(node, "what", &errors);

		if (errors) {
			ux_err("errors while parsing patch-type file \"%s\"\n", patch_file);
			free((void *)patch->filename);
			free((void *)patch->start_sector);
			free((void *)patch->value);
			free((void *)patch->what);
			free(patch);
			continue;
		}

		list_append(ops, &patch->node);
	}

	patches_loaded = true;

	return 0;
}

int patch_load(struct list_head *ops, const char *patch_file)
{
	xmlDoc *doc;
	int ret;

	doc = xmlReadFile(patch_file, NULL, 0);
	if (!doc) {
		ux_err("failed to parse patch-type file \"%s\"\n", patch_file);
		return -EINVAL;
	}

	ret = patch_load_xml(ops, doc, patch_file);

	xmlFreeDoc(doc);

	return ret;
}
