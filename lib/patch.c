// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * All rights reserved.
 */
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <unistd.h>

#include "patch.h"
#include "qdl.h"

void patch_init(struct qdl_device *qdl)
{
	list_init(&qdl->patch_ctx.patches);
	qdl->patch_ctx.loaded = false;
}

int patch_load(struct qdl_device *qdl, const char *patch_file)
{
	struct patch_ctx *ctx = &qdl->patch_ctx;
	struct patch *patch;
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;
	int errors;

	doc = xmlReadFile(patch_file, NULL, XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
	if (!doc) {
		ux_err("failed to parse patch-type file \"%s\"\n", patch_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	for (node = root->children; node ; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name, (xmlChar *)"patch")) {
			ux_err("unrecognized tag \"%s\" in patch-type file, ignoring\n", node->name);
			continue;
		}

		errors = 0;

		patch = calloc(1, sizeof(struct patch));

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

		list_add(&ctx->patches, &patch->node);
	}

	xmlFreeDoc(doc);

	ctx->loaded = true;

	return 0;
}

int patch_execute(struct qdl_device *qdl,
		  int (*apply)(struct qdl_device *qdl, struct patch *patch))
{
	struct patch_ctx *ctx = &qdl->patch_ctx;
	struct patch *patch;
	unsigned int count = 0;
	unsigned int idx = 0;
	int ret;

	if (!ctx->loaded)
		return 0;

	list_for_each_entry(patch, &ctx->patches, node) {
		if (!patch->filename)
			continue;

		if (!strcmp(patch->filename, "DISK"))
			count++;
	}

	list_for_each_entry(patch, &ctx->patches, node) {
		if (!patch->filename)
			continue;

		if (strcmp(patch->filename, "DISK"))
			continue;

		ret = apply(qdl, patch);
		if (ret)
			return ret;

		ux_progress("Applying patches", idx++, count);
	}

	ux_info("%d patches applied\n", idx);

	return 0;
}

void patch_free(struct qdl_device *qdl)
{
	struct patch_ctx *ctx = &qdl->patch_ctx;
	struct patch *patch;
	struct patch *next;

	list_for_each_entry_safe(patch, next, &ctx->patches, node) {
		free((void *)patch->filename);
		free((void *)patch->start_sector);
		free((void *)patch->value);
		free((void *)patch->what);
		free(patch);
	}

	list_init(&ctx->patches);
	ctx->loaded = false;
}
