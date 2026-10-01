// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <ctype.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlstring.h>

#include "contents.h"
#include "file.h"
#include "firehose.h"
#include "pathbuf.h"
#include "meta_cli.h"
#include "qdl.h"

#ifdef _WIN32
#include <windows.h>
#define ROOT_PATH_TAG "windows_root_path"
#else
#define ROOT_PATH_TAG "linux_root_path"
#endif

enum contents_file_type {
	CONTENTS_FILE_OTHER,
	CONTENTS_FILE_PROGRAM,
	CONTENTS_FILE_PATCH,
	CONTENTS_FILE_DEVICE_PROGRAMMER,
	CONTENTS_FILE_PROGRAMMER_XML,
};

enum firehose_type {
	FIREHOSE_TYPE_NONE,
	FIREHOSE_TYPE_LITE,
	FIREHOSE_TYPE_TRUE,
};

struct contents_entry {
	enum contents_file_type file_type;

	enum qdl_storage_type storage_type;
	char *flavor;

	char *filename;
	struct pathbuf path;

	enum firehose_type firehose_type;

	struct list_head node;
};

struct contents {
	struct list_head entries;
	struct pathbuf base_dir;

	char **flavors;
	size_t num_flavors;
};

static int contents_populate_from_metacli(struct contents *contents, struct metacli_ctx *ctx,
					   enum qdl_storage_type storage_type, const char *flavor,
					   const char *sku);
struct contents_filter {
	struct contents *contents;

	enum qdl_storage_type storage_type;
	const char *flavor;
};

struct contents_selector {
	enum qdl_storage_type storage_type;
	const char *flavor;
	const char *sku;
};

static const char *contents_storage_name(enum qdl_storage_type storage)
{
	const char *name = encode_storage_type(storage);

	return name ? name : "unknown";
}

static char *contents_node_get_text(xmlNode *node)
{
	const xmlChar *start;
	const xmlChar *end;
	xmlChar *str;
	size_t len;
	char *ret;

	str = xmlNodeGetContent(node);
	if (!str)
		return NULL;

	for (start = str; *start && isspace((unsigned char)*start); start++)
		;

	for (end = start + xmlStrlen(start); end > start &&
	     isspace((unsigned char)end[-1]); end--)
		;

	len = end - start;
	ret = calloc(1, len + 1);
	if (!ret)
		goto out_free_str;

	memcpy(ret, start, len);

out_free_str:
	xmlFree(str);

	return ret;
}

static bool contents_entry_is_ignored(xmlNode *node)
{
	xmlChar *ignore;
	bool ret;

	ignore = xmlGetProp(node, (xmlChar *)"ignore");
	if (!ignore)
		return false;

	ret = !xmlStrcmp(ignore, (xmlChar *)"true");

	xmlFree(ignore);

	return ret;
}

static int contents_parse_pf(struct contents *contents, xmlNode *node)
{
	char **new_flavors;
	char *name = NULL;

	for (; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    !xmlStrcmp(node->name, (xmlChar *)"name")) {
			name = contents_node_get_text(node);
			break;
		}
	}

	if (!name)
		return -1;

	new_flavors = realloc(contents->flavors, (contents->num_flavors + 1) * sizeof(*new_flavors));
	if (!new_flavors) {
		free(name);
		return -1;
	}

	contents->flavors = new_flavors;
	contents->flavors[contents->num_flavors++] = name;

	return 0;
}

static int contents_parse_pfs(struct contents *contents, xmlNode *node)
{
	int ret;

	for (; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (!xmlStrcmp(node->name, (xmlChar *)"pf")) {
			ret = contents_parse_pf(contents, node->children);
			if (ret < 0) {
				ux_err("failed to parse product flavor definition\n");
				return -1;
			}
		}
	}

	return 0;
}

static enum contents_file_type contents_detect_file_type(xmlNode *node,
							 enum firehose_type *fh_type)
{
	enum contents_file_type type = CONTENTS_FILE_OTHER;
	xmlChar *firehose_type;

	if (!xmlStrcmp(node->name, (xmlChar *)"partition_file"))
		type = CONTENTS_FILE_PROGRAM;
	if (!xmlStrcmp(node->name, (xmlChar *)"partition_patch_file"))
		type = CONTENTS_FILE_PATCH;
	if (!xmlStrcmp(node->name, (xmlChar *)"device_programmer"))
		type = CONTENTS_FILE_DEVICE_PROGRAMMER;

	firehose_type = xmlGetProp(node, (xmlChar *)"firehose_type");
	if (firehose_type) {
		if (!xmlStrcmp(firehose_type, (xmlChar *)"true")) {
			type = CONTENTS_FILE_PROGRAMMER_XML;
			*fh_type = FIREHOSE_TYPE_TRUE;
		} else if (!xmlStrcmp(firehose_type, (xmlChar *)"lite")) {
			*fh_type = FIREHOSE_TYPE_LITE;
		} else {
			*fh_type = FIREHOSE_TYPE_NONE;
		}
	}

	xmlFree(firehose_type);

	return type;
}

static enum qdl_storage_type contents_detect_storage_type(xmlNode *node)
{
	enum qdl_storage_type type = QDL_STORAGE_UNKNOWN;
	xmlChar *storage;

	storage = xmlGetProp(node, (xmlChar *)"storage_type");
	if (storage) {
		type = decode_storage_type((char *)storage);
		xmlFree(storage);
	}

	return type;
}

static int contents_expand_path_vars(struct pathbuf *path)
{
	char *head = &path->buf[0];
	char *tail = head;
	char *colon;
	char *end;

	/*
	 * Paths can contain "${var:default}" entries, which allow overriding
	 * portions of the path. As this is not supported, replace each with
	 * "default".
	 */
	while (*head) {
		if (head[0] == '$' && head[1] == '{') {
			colon = strchr(head + 2, ':');
			end = colon ? strchr(colon + 1, '}') : NULL;
			if (colon && end) {
				head = colon + 1;
				while (head < end)
					*tail++ = *head++;
				head = end + 1;
				continue;
			}
		}
		*tail++ = *head++;
	}

	*tail = '\0';
	path->len = tail - path->buf;

	return 0;
}

static int contents_parse_file_names(struct contents *contents, xmlNode *node,
				     enum contents_file_type file_type,
				     struct pathbuf *current_path, char *path_flavor,
				     enum qdl_storage_type storage,
				     enum firehose_type firehose_type)
{
	struct contents_entry *entry;
	struct pathbuf full_path;
	xmlNode *child;
	char *filename;
	int ret;

	for (child = node->children; child; child = child->next) {
		if (child->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(child->name, (xmlChar *)"file_name"))
			continue;

		filename = contents_node_get_text(child);
		if (!filename)
			return -1;

		/* Ignore filenames with wildcards */
		if (strstr(filename, "*")) {
			free(filename);
			continue;
		}

		qdl_pathbuf_dup(&full_path, current_path);
		ret = qdl_pathbuf_push(&full_path, filename);
		if (ret < 0) {
			free(filename);
			return -1;
		}

		contents_expand_path_vars(&full_path);

		entry = calloc(1, sizeof(*entry));
		if (!entry) {
			free(filename);
			return -1;
		}

		entry->file_type = file_type;
		entry->storage_type = storage;
		if (path_flavor) {
			entry->flavor = strdup(path_flavor);
			if (!entry->flavor) {
				free(entry);
				free(filename);
				return -1;
			}
		}
		entry->firehose_type = firehose_type;

		entry->filename = filename;
		qdl_pathbuf_dup(&entry->path, &full_path);

		list_append(&contents->entries, &entry->node);
	}

	return 0;
}

static int contents_parse_entry(struct contents *contents, xmlNode *node,
				struct pathbuf *build_root)
{
	enum contents_file_type file_type;
	enum firehose_type firehose_type = FIREHOSE_TYPE_NONE;
	enum qdl_storage_type storage;
	struct pathbuf file_path;
	xmlNode *child;
	char *flavor;
	char *path;
	int ret;

	if (!build_root) {
		ux_err("entry has no build root path in contents.xml\n");
		return -1;
	}

	if (contents_entry_is_ignored(node))
		return 0;

	file_type = contents_detect_file_type(node, &firehose_type);
	storage = contents_detect_storage_type(node);

	for (child = node->children; child; child = child->next) {
		if (child->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(child->name, (xmlChar *)"file_path"))
			continue;

		path = contents_node_get_text(child);
		if (!path)
			return -1;
		flavor = (char *)xmlGetProp(child, (xmlChar *)"flavor");

		qdl_pathbuf_dup(&file_path, build_root);
		ret = qdl_pathbuf_push(&file_path, path);
		if (ret < 0) {
			xmlFree(flavor);
			free(path);
			return -1;
		}

		ret = contents_parse_file_names(contents, node, file_type, &file_path, flavor, storage,
						firehose_type);

		xmlFree(flavor);
		free(path);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static bool contents_is_entry_node(xmlNode *node)
{
	return !xmlStrcmp(node->name, (xmlChar *)"download_file") ||
	       !xmlStrcmp(node->name, (xmlChar *)"file_ref") ||
	       !xmlStrcmp(node->name, (xmlChar *)"partition_file") ||
	       !xmlStrcmp(node->name, (xmlChar *)"partition_patch_file") ||
	       !xmlStrcmp(node->name, (xmlChar *)"device_programmer");
}

static int contents_parse_builds(struct contents *contents, xmlNode *node, struct pathbuf *root_path)
{
	char *root;
	int ret;

	for (; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (!xmlStrcmp(node->name, (xmlChar *)"build")) {
			struct pathbuf build_root = {0};

			ret = contents_parse_builds(contents, node->children, &build_root);
			if (ret < 0)
				return ret;

			continue;
		}

		if (!xmlStrcmp(node->name, (xmlChar *)ROOT_PATH_TAG)) {
			root = contents_node_get_text(node);
			if (!root)
				return -1;

			if (!root_path) {
				ux_err("linux_root_path without active build context\n");
				free(root);
				return -1;
			}

			qdl_pathbuf_dup(root_path, &contents->base_dir);
			ret = qdl_pathbuf_push(root_path, root);
			free(root);
			if (ret < 0)
				return -1;
		} else if (contents_is_entry_node(node)) {
			ret = contents_parse_entry(contents, node, root_path);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int contents_parse_nodes(struct contents *contents, xmlNode *node)
{
	int ret;

	for (; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (!xmlStrcmp(node->name, (xmlChar *)"product_flavors")) {
			ret = contents_parse_pfs(contents, node->children);
			if (ret < 0)
				return ret;
		} else if (!xmlStrcmp(node->name, (xmlChar *)"builds_flat")) {
			ret = contents_parse_builds(contents, node->children, NULL);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int contents_get_base_dir(struct pathbuf *base_dir, const char *filename)
{
	qdl_pathbuf_reset(base_dir);
	qdl_pathbuf_push(base_dir, filename);
	qdl_pathbuf_dirname(base_dir);

	return 0;
}

int contents_load_xml(struct contents *contents, const char *filename)
{
	xmlNode *root;
	xmlDoc *doc;
	int ret;

	doc = xmlReadFile(filename, NULL, 0);
	if (!doc) {
		ux_err("failed to parse contents file \"%s\"\n", filename);
		return -1;
	}

	root = xmlDocGetRootElement(doc);
	if (!root || xmlStrcmp(root->name, (xmlChar *)"contents")) {
		ux_err("specified file \"%s\" is not a contents.xml document\n", filename);
		xmlFreeDoc(doc);
		return -1;
	}

	ret = contents_get_base_dir(&contents->base_dir, filename);
	if (ret < 0)
		goto err_free_doc;

	ret = contents_parse_nodes(contents, root->children);
	if (ret < 0)
		goto err_free_doc;

	xmlFreeDoc(doc);

	if (list_empty(&contents->entries))
		ux_info("contents: no file entries parsed from \"%s\"\n", filename);

	return 0;

err_free_doc:
	xmlFreeDoc(doc);
	return -1;
}

static int contents_find_programmers(struct contents *contents,
				     struct contents_filter *filter,
				     struct sahara_image *images)
{
	struct contents_filter default_filter = { .contents = contents };
	struct contents_entry *entry;
	struct sahara_image blob;
	int ret;

	if (!filter)
		filter = &default_filter;

	list_for_each_entry(entry, &contents->entries, node) {
		if (entry->file_type != CONTENTS_FILE_PROGRAMMER_XML)
			continue;
		if (filter->storage_type != QDL_STORAGE_UNKNOWN &&
		    entry->storage_type != QDL_STORAGE_UNKNOWN &&
		    entry->storage_type != filter->storage_type)
			continue;
		if (entry->flavor && filter->flavor &&
		    strcmp(entry->flavor, filter->flavor))
			continue;

		ret = load_sahara_image(NULL, qdl_pathbuf_str(&entry->path), &blob);
		if (ret < 0) {
			ux_err("unable to open \"%s\" for reading\n", qdl_pathbuf_str(&entry->path));
			continue;
		}

		ret = decode_sahara_config(&blob, images, filter);
		if (ret == 0) {
			ux_err("%s is not a programmer xml\n", qdl_pathbuf_str(&entry->path));
			sahara_images_free(&blob, 1);
			continue;
		} else if (ret < 0) {
			ux_err("failed to parse programmer xml \"%s\"\n", qdl_pathbuf_str(&entry->path));
			sahara_images_free(&blob, 1);
			return -1;
		}

		sahara_images_free(&blob, 1);
		return 0;
	}

	list_for_each_entry(entry, &contents->entries, node) {
		if (entry->file_type != CONTENTS_FILE_DEVICE_PROGRAMMER)
			continue;
		if (filter->storage_type != QDL_STORAGE_UNKNOWN &&
		    entry->storage_type != QDL_STORAGE_UNKNOWN &&
		    entry->storage_type != filter->storage_type)
			continue;
		if (entry->flavor && filter->flavor &&
		    strcmp(entry->flavor, filter->flavor))
			continue;

		if (entry->firehose_type == FIREHOSE_TYPE_LITE)
			continue;

		ret = load_sahara_image(NULL, qdl_pathbuf_str(&entry->path),
					&images[SAHARA_ID_EHOSTDL_IMG]);
		if (ret < 0) {
			ux_err("unable to open \"%s\" for reading\n", qdl_pathbuf_str(&entry->path));
			continue;
		}

		return 0;
	}

	ux_err("no programmer definitions found\n");
	return -1;
}

static bool contents_flavor_matches(const char *a, const char *b)
{
	return (!a && !b) || (a && b && !strcmp(a, b));
}

static bool contents_selector_is_known(struct contents_selector *selectors, size_t count,
				       enum qdl_storage_type storage_type,
				       const char *flavor)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (selectors[i].storage_type == storage_type &&
		    contents_flavor_matches(selectors[i].flavor, flavor))
			return true;
	}

	return false;
}

static bool contents_storage_is_selected(struct contents_selector *selectors, size_t count,
					 enum qdl_storage_type storage_type)
{
	size_t selector_idx;

	for (selector_idx = 0; selector_idx < count; selector_idx++) {
		if (selectors[selector_idx].storage_type == storage_type)
			return true;
	}

	return false;
}

static bool contents_storage_has_flavored_entries(struct contents *contents,
						  enum qdl_storage_type storage_type)
{
	struct contents_entry *entry;

	list_for_each_entry(entry, &contents->entries, node) {
		if (entry->file_type != CONTENTS_FILE_PROGRAM &&
		    entry->file_type != CONTENTS_FILE_PATCH)
			continue;
		if (entry->storage_type != storage_type)
			continue;
		if (entry->flavor)
			return true;
	}

	return false;
}

static size_t contents_collect_valid_selectors(struct contents *contents,
					       struct contents_selector **contents_selectors)
{
	struct contents_selector *new_selectors;
	struct contents_selector *selectors = NULL;
	struct contents_entry *entry;
	size_t count = 0;

	list_for_each_entry(entry, &contents->entries, node) {
		if (entry->file_type != CONTENTS_FILE_PROGRAM &&
		    entry->file_type != CONTENTS_FILE_PATCH)
			continue;
		if (entry->storage_type == QDL_STORAGE_UNKNOWN)
			continue;
		if (!entry->flavor && contents_storage_has_flavored_entries(contents, entry->storage_type))
			continue;
		if (contents_selector_is_known(selectors, count, entry->storage_type, entry->flavor))
			continue;

		new_selectors = realloc(selectors, (count + 1) * sizeof(*selectors));
		if (!new_selectors) {
			free(selectors);
			return 0;
		}

		selectors = new_selectors;
		selectors[count].storage_type = entry->storage_type;
		selectors[count].flavor = entry->flavor;
		selectors[count].sku = NULL;
		count++;
	}

	*contents_selectors = selectors;

	return count;
}

static void contents_print_valid_selectors(struct contents_selector *selectors, size_t count)
{
	size_t selector_idx;

	ux_err("valid storage/flavor combinations:\n");
	for (selector_idx = 0; selector_idx < count; selector_idx++) {
		if (selectors[selector_idx].flavor)
			ux_err("  %s/%s\n", contents_storage_name(selectors[selector_idx].storage_type),
			       selectors[selector_idx].flavor);
		else
			ux_err("  %s\n", contents_storage_name(selectors[selector_idx].storage_type));
	}
}

static bool contents_flavor_is_valid(struct contents *contents, const char *flavor)
{
	size_t flavor_idx;

	for (flavor_idx = 0; flavor_idx < contents->num_flavors; flavor_idx++) {
		if (!strcmp(flavor, contents->flavors[flavor_idx]))
			return true;
	}

	return false;
}

static size_t contents_select_by_storage(struct contents_selector *valid_selectors,
					 size_t num_valid_selectors,
					 enum qdl_storage_type storage_type,
					 struct contents_selector *selector)
{
	size_t valid_idx;
	size_t matches = 0;

	for (valid_idx = 0; valid_idx < num_valid_selectors; valid_idx++) {
		if (valid_selectors[valid_idx].storage_type != storage_type)
			continue;

		*selector = valid_selectors[valid_idx];
		matches++;
	}

	return matches;
}

static size_t contents_select_by_flavor(struct contents_selector *valid_selectors,
					size_t num_valid_selectors,
					const char *flavor,
					struct contents_selector *selector)
{
	size_t valid_idx;
	size_t matches = 0;

	for (valid_idx = 0; valid_idx < num_valid_selectors; valid_idx++) {
		if (!valid_selectors[valid_idx].flavor ||
		    strcmp(valid_selectors[valid_idx].flavor, flavor))
			continue;

		*selector = valid_selectors[valid_idx];
		matches++;
	}

	return matches;
}

static int contents_decode_selectors(struct contents *contents, char *pattern,
				     struct contents_selector **contents_selectors)
{
	struct contents_selector *valid_selectors = NULL;
	struct contents_selector *new_selectors;
	struct contents_selector *selectors = NULL;
	struct contents_selector selector;
	enum qdl_storage_type storage;
	size_t num_valid_selectors;
	char *flavor;
	char *sku;
	size_t count = 0;
	char *token;
	char *save;
	char *sep;
	size_t matches;

	num_valid_selectors = contents_collect_valid_selectors(contents, &valid_selectors);
	if (!num_valid_selectors) {
		ux_err("contents.xml does not provide any valid storage/flavor combinations\n");
		return -1;
	}

	if (!pattern) {
		if (num_valid_selectors == 1) {
			*contents_selectors = valid_selectors;
			return 1;
		}

		if (num_valid_selectors > 1) {
			ux_err("contents.xml contains multiple storage/flavor combinations; select one or more with ::<storage>/<flavor>\n");
			contents_print_valid_selectors(valid_selectors, num_valid_selectors);
			free(valid_selectors);
			return -1;
		}

		free(valid_selectors);
		return -1;
	}

	if (!pattern[0]) {
		ux_err("empty storage/flavor selector\n");
		goto err_free_valid_selectors;
	}

	for (token = strtok_r(pattern, ",", &save); token; token = strtok_r(NULL, ",", &save)) {
		new_selectors = realloc(selectors, (count + 1) * sizeof(*selectors));
		if (!new_selectors)
			goto err_free_selectors;

		selectors = new_selectors;

		if (!token[0]) {
			ux_err("empty storage/flavor selector\n");
			goto err_free_selectors;
		}

		sep = strchr(token, '/');
		if (!sep) {
			storage = decode_storage_type(token);
			if (storage != QDL_STORAGE_UNKNOWN) {
				matches = contents_select_by_storage(valid_selectors, num_valid_selectors,
								     storage, &selector);
				if (matches == 1)
					goto append_selector;

				if (!matches)
					ux_err("storage type \"%s\" has no valid flavor in contents.xml\n", token);
				else
					ux_err("storage type \"%s\" is ambiguous; specify a flavor\n", token);

				contents_print_valid_selectors(valid_selectors, num_valid_selectors);
				goto err_free_selectors;
			}

			matches = contents_select_by_flavor(valid_selectors, num_valid_selectors,
							    token, &selector);
			if (matches == 1)
				goto append_selector;

			if (matches > 1)
				ux_err("flavor \"%s\" is ambiguous; specify a storage type\n", token);
			else if (contents_flavor_is_valid(contents, token))
				ux_err("flavor \"%s\" has no valid storage type in contents.xml\n", token);
			else
				ux_err("unknown storage type or flavor \"%s\"\n", token);

			contents_print_valid_selectors(valid_selectors, num_valid_selectors);
			goto err_free_selectors;
		}

		*sep = '\0';

		flavor = sep + 1;
		if (!token[0]) {
			ux_err("missing storage selector for flavor \"%s\"\n", flavor);
			goto err_free_selectors;
		}
		if (!flavor[0]) {
			ux_err("invalid flavor selection for storage \"%s\"\n", token);
			goto err_free_selectors;
		}

		sku = strchr(flavor, '/');
		if (sku) {
			*sku = '\0';
			sku++;
			if (!sku[0]) {
				ux_err("invalid sku selection for storage \"%s\" flavor \"%s\"\n",
				       token, flavor);
				goto err_free_selectors;
			}
		}

		if (!contents_flavor_is_valid(contents, flavor)) {
			ux_err("invalid flavor \"%s\" requested\n", flavor);
			ux_err("valid flavors:\n");

			for (size_t flavor_idx = 0; flavor_idx < contents->num_flavors; flavor_idx++)
				ux_err("  %s\n", contents->flavors[flavor_idx]);
			goto err_free_selectors;
		}

		storage = decode_storage_type(token);
		if (storage == QDL_STORAGE_UNKNOWN) {
			ux_err("unknown storage type \"%s\"\n", token);
			goto err_free_selectors;
		}

		if (!contents_selector_is_known(valid_selectors, num_valid_selectors, storage, flavor)) {
			ux_err("storage/flavor combination \"%s/%s\" not mentioned in contents.xml\n",
			       contents_storage_name(storage), flavor);
			contents_print_valid_selectors(valid_selectors, num_valid_selectors);
			goto err_free_selectors;
		}

		selector.storage_type = storage;
		selector.flavor = flavor;
		selector.sku = NULL;

append_selector:
		if (contents_storage_is_selected(selectors, count, selector.storage_type)) {
			ux_err("storage type \"%s\" selected multiple times\n",
			       contents_storage_name(selector.storage_type));
			goto err_free_selectors;
		}

		selectors[count] = selector;
		count++;
	}

	*contents_selectors = selectors;
	free(valid_selectors);

	return count;

err_free_selectors:
	free(selectors);
err_free_valid_selectors:
	free(valid_selectors);

	return -1;
}

/**
 * metacli_collect_valid_selectors() - build valid_selectors from meta_cli
 * @ctx: meta_cli context
 * @contents: contents->flavors/num_flavors are populated as a side effect
 * @contents_selectors: output, same shape as contents_collect_valid_selectors()
 *
 * contents_collect_valid_selectors() scans contents->entries, which do not
 * exist yet before contents_populate_from_metacli() runs. This builds the
 * same (storage_type, flavor) cross product directly from meta_cli's
 * get_storage_types/get_product_flavors instead.
 *
 * Returns: number of selectors, 0 on error
 */
static size_t metacli_collect_valid_selectors(struct metacli_ctx *ctx, struct contents *contents,
					       struct contents_selector **contents_selectors)
{
	struct json_value *storage_types = NULL;
	struct json_value *flavors = NULL;
	struct contents_selector *new_selectors;
	struct contents_selector *selectors = NULL;
	const char *storage_str, *flavor_str;
	char *cmd_argv[2];
	int storage_count, flavor_count, flavor_iterations;
	int i, j;
	size_t count = 0;

	cmd_argv[0] = "get_storage_types";
	cmd_argv[1] = NULL;
	if (metacli_run_json(ctx, cmd_argv, &storage_types) < 0)
		return 0;

	storage_count = json_count_children(storage_types);
	if (storage_count <= 0) {
		json_free(storage_types);
		return 0;
	}

	cmd_argv[0] = "get_product_flavors";
	cmd_argv[1] = NULL;
	if (metacli_run_json(ctx, cmd_argv, &flavors) < 0) {
		json_free(storage_types);
		return 0;
	}

	flavor_count = json_count_children(flavors);

	if (flavor_count > 0) {
		contents->flavors = calloc(flavor_count, sizeof(char *));
		if (!contents->flavors) {
			json_free(storage_types);
			json_free(flavors);
			return 0;
		}

		for (i = 0; i < flavor_count; i++) {
			flavor_str = json_get_element_string(flavors, i);
			if (!flavor_str)
				continue;

			contents->flavors[contents->num_flavors] = strdup(flavor_str);
			if (!contents->flavors[contents->num_flavors]) {
				json_free(storage_types);
				json_free(flavors);
				return 0;
			}
			contents->num_flavors++;
		}
	}

	flavor_iterations = contents->num_flavors > 0 ? (int)contents->num_flavors : 1;

	for (i = 0; i < storage_count; i++) {
		enum qdl_storage_type storage;

		storage_str = json_get_element_string(storage_types, i);
		if (!storage_str)
			continue;

		storage = decode_storage_type(storage_str);
		if (storage == QDL_STORAGE_UNKNOWN)
			continue;

		for (j = 0; j < flavor_iterations; j++) {
			new_selectors = realloc(selectors, (count + 1) * sizeof(*selectors));
			if (!new_selectors) {
				free(selectors);
				json_free(storage_types);
				json_free(flavors);
				return 0;
			}
			selectors = new_selectors;
			selectors[count].storage_type = storage;
			/* contents->flavors[], not the json string -- it must
			 * outlive this call, the json_value does not.
			 */
			selectors[count].flavor = contents->num_flavors > 0 ?
						   contents->flavors[j] : NULL;
			selectors[count].sku = NULL;
			count++;
		}
	}

	*contents_selectors = selectors;

	json_free(storage_types);
	json_free(flavors);

	return count;
}

/**
 * metacli_contents_decode_selectors() - contents_decode_selectors(), but
 *	validated against meta_cli instead of contents->entries
 * @ctx: meta_cli context
 * @contents: contents->flavors/num_flavors are populated as a side effect
 * @pattern: pattern string to split in place, or NULL
 * @sku: output, pointer into @pattern, or NULL
 * @contents_selectors: output selector array, same shape as
 *	contents_decode_selectors()
 *
 * Needed because contents_populate_from_metacli() must know storage/
 * flavor/sku before it has created any contents->entries for
 * contents_decode_selectors() to validate against -- this does the exact
 * same parsing and validation, just sourced from meta_cli's
 * get_storage_types/get_product_flavors/get_sku_config_list. Its output
 * is final: on success, contents_load() uses it directly and does not
 * also run contents_decode_selectors().
 *
 * Returns: number of selectors on success, -1 on error
 */
static int metacli_contents_decode_selectors(struct metacli_ctx *ctx, struct contents *contents,
					      char *pattern,
					      struct contents_selector **contents_selectors)
{
	struct contents_selector *valid_selectors = NULL;
	struct contents_selector *new_selectors;
	struct contents_selector *selectors = NULL;
	struct contents_selector selector;
	enum qdl_storage_type storage;
	size_t num_valid_selectors;
	char *flavor;
	char *sku;
	size_t count = 0;
	char *token;
	char *save;
	char *sep;
	size_t matches;

	num_valid_selectors = metacli_collect_valid_selectors(ctx, contents, &valid_selectors);
	if (!num_valid_selectors) {
		ux_err("meta_cli does not provide any valid storage/flavor combinations\n");
		return -1;
	}

	if (!pattern) {
		if (num_valid_selectors == 1) {
			*contents_selectors = valid_selectors;
			return 1;
		}

		if (num_valid_selectors > 1) {
			ux_err("meta_cli reports multiple storage/flavor combinations; select one or more with ::<storage>/<flavor>\n");
			contents_print_valid_selectors(valid_selectors, num_valid_selectors);
			free(valid_selectors);
			return -1;
		}

		free(valid_selectors);
		return -1;
	}

	if (!pattern[0]) {
		ux_err("empty storage/flavor selector\n");
		goto err_free_valid_selectors;
	}

	for (token = strtok_r(pattern, ",", &save); token; token = strtok_r(NULL, ",", &save)) {
		new_selectors = realloc(selectors, (count + 1) * sizeof(*selectors));
		if (!new_selectors)
			goto err_free_selectors;

		selectors = new_selectors;

		if (!token[0]) {
			ux_err("empty storage/flavor selector\n");
			goto err_free_selectors;
		}

		sep = strchr(token, '/');
		if (!sep) {
			storage = decode_storage_type(token);
			if (storage != QDL_STORAGE_UNKNOWN) {
				matches = contents_select_by_storage(valid_selectors, num_valid_selectors,
								     storage, &selector);
				if (matches == 1)
					goto append_selector;

				if (!matches)
					ux_err("storage type \"%s\" has no valid flavor in meta_cli\n", token);
				else
					ux_err("storage type \"%s\" is ambiguous; specify a flavor\n", token);

				contents_print_valid_selectors(valid_selectors, num_valid_selectors);
				goto err_free_selectors;
			}

			matches = contents_select_by_flavor(valid_selectors, num_valid_selectors,
							    token, &selector);
			if (matches == 1)
				goto append_selector;

			if (matches > 1)
				ux_err("flavor \"%s\" is ambiguous; specify a storage type\n", token);
			else if (contents_flavor_is_valid(contents, token))
				ux_err("flavor \"%s\" has no valid storage type in meta_cli\n", token);
			else
				ux_err("unknown storage type or flavor \"%s\"\n", token);

			contents_print_valid_selectors(valid_selectors, num_valid_selectors);
			goto err_free_selectors;
		}

		*sep = '\0';

		flavor = sep + 1;
		if (!token[0]) {
			ux_err("missing storage selector for flavor \"%s\"\n", flavor);
			goto err_free_selectors;
		}
		if (!flavor[0]) {
			ux_err("invalid flavor selection for storage \"%s\"\n", token);
			goto err_free_selectors;
		}

		sku = strchr(flavor, '/');
		if (sku) {
			*sku = '\0';
			sku++;
			if (!sku[0]) {
				ux_err("invalid sku selection for storage \"%s\" flavor \"%s\"\n",
				       token, flavor);
				goto err_free_selectors;
			}
		}

		if (!contents_flavor_is_valid(contents, flavor)) {
			ux_err("invalid flavor \"%s\" requested\n", flavor);
			ux_err("valid flavors:\n");

			for (size_t flavor_idx = 0; flavor_idx < contents->num_flavors; flavor_idx++)
				ux_err("  %s\n", contents->flavors[flavor_idx]);
			goto err_free_selectors;
		}

		storage = decode_storage_type(token);
		if (storage == QDL_STORAGE_UNKNOWN) {
			ux_err("unknown storage type \"%s\"\n", token);
			goto err_free_selectors;
		}

		if (!contents_selector_is_known(valid_selectors, num_valid_selectors, storage, flavor)) {
			ux_err("storage/flavor combination \"%s/%s\" not reported by meta_cli\n",
			       contents_storage_name(storage), flavor);
			contents_print_valid_selectors(valid_selectors, num_valid_selectors);
			goto err_free_selectors;
		}

		selector.storage_type = storage;
		selector.flavor = flavor;
		selector.sku = sku;

append_selector:
		if (contents_storage_is_selected(selectors, count, selector.storage_type)) {
			ux_err("storage type \"%s\" selected multiple times\n",
			       contents_storage_name(selector.storage_type));
			goto err_free_selectors;
		}

		selectors[count] = selector;
		count++;
	}

	*contents_selectors = selectors;
	free(valid_selectors);

	/*
	 * Validate the sku against meta_cli, same as the storage/flavor
	 * validation above. get_sku_config_list may not exist on older
	 * meta_cli builds, and an unrecognized sku is just a user typo --
	 * neither is fatal, it just means proceeding without sku filtering.
	 */
	if (count && selectors[0].sku) {
		struct json_value *skus = NULL;
		char *sku_cmd_argv[2] = { "get_sku_config_list", NULL };
		bool sku_ok = false;

		if (metacli_run_json(ctx, sku_cmd_argv, &skus) < 0) {
			ux_info("meta_cli: get_sku_config_list unavailable, ignoring sku \"%s\"\n",
				selectors[0].sku);
		} else {
			int sku_count = json_count_children(skus);
			int sku_idx;

			for (sku_idx = 0; sku_idx < sku_count; sku_idx++) {
				const char *sku_str = json_get_element_string(skus, sku_idx);

				if (sku_str && !strcmp(sku_str, selectors[0].sku)) {
					sku_ok = true;
					break;
				}
			}
			if (!sku_ok)
				ux_info("meta_cli: sku \"%s\" not in get_sku_config_list, ignoring\n",
					selectors[0].sku);
			json_free(skus);
		}

		if (!sku_ok)
			selectors[0].sku = NULL;
	}

	return count;

err_free_selectors:
	free(selectors);
err_free_valid_selectors:
	free(valid_selectors);

	return -1;
}

/**
 * contents_free_entries() - free all entries and flavors
 * @contents: contents database to clear
 */
static void contents_free_entries(struct contents *contents)
{
	struct contents_entry *entry;
	struct contents_entry *next;
	size_t flavor_idx;

	for (flavor_idx = 0; flavor_idx < contents->num_flavors; flavor_idx++)
		free(contents->flavors[flavor_idx]);
	free(contents->flavors);

	list_for_each_entry_safe(entry, next, &contents->entries, node) {
		free(entry->filename);
		free(entry->flavor);
		free(entry);
	}
}

int contents_load(struct list_head *ops, const char *filename, char *specifier,
		  struct sahara_image *images, const char *incdir)
{
	struct contents_filter filter = {};
	struct contents_entry *entry;
	struct contents contents = {};
	struct metacli_ctx *metacli_ctx = NULL;
	enum qdl_storage_type storage_type;
	struct contents_selector *selectors = NULL;
	struct firehose_op *op;
	const char *flavor;
	int num_selectors;
	char *pattern = specifier;
	char *pattern_copy = NULL;
	bool populated = false;
	int ret;
	int i;

	list_init(&contents.entries);

	ux_debug("contents_load: filename='%s' pattern='%s'\n", filename, pattern ? pattern : "NULL");

	/* Try meta_cli first if available */
	if (metacli_locate(filename, &metacli_ctx)) {
		int decode_ret;

		ux_info("using meta_cli for contents.xml\n");

		/*
		 * Decode on a copy: metacli_contents_decode_selectors() splits
		 * it in place, and contents_decode_selectors() below still
		 * needs the original intact if this falls back to XML.
		 */
		pattern_copy = pattern ? strdup(pattern) : NULL;

		decode_ret = metacli_contents_decode_selectors(metacli_ctx, &contents, pattern_copy,
							       &selectors);
		if (decode_ret > 0) {
			num_selectors = decode_ret;
			ret = contents_populate_from_metacli(&contents, metacli_ctx,
							      selectors[0].storage_type,
							      selectors[0].flavor, selectors[0].sku);
		} else {
			ret = -1;
		}

		metacli_ctx_free(metacli_ctx);

		if (ret == 0) {
			/* meta_cli succeeded; selectors are already final */
			populated = true;
		} else {
			ux_info("meta_cli failed, falling back to XML parser\n");
			free(selectors);
			selectors = NULL;
			free(pattern_copy);
			pattern_copy = NULL;
			contents_free_entries(&contents);
			list_init(&contents.entries);
			contents.num_flavors = 0;
			contents.flavors = NULL;
		}
	}

	if (!populated) {
		ret = contents_load_xml(&contents, filename);
		if (ret < 0)
			goto out_free_contents;

		ret = contents_decode_selectors(&contents, pattern, &selectors);
		if (ret < 0)
			goto out_free_contents;
		num_selectors = ret;
		if (num_selectors == 0) {
			ux_err("contents.xml does not provide any valid storage/flavor combinations\n");
			ret = -1;
			goto out_free_contents;
		}
	}

	filter.contents = &contents;
	filter.storage_type = selectors[0].storage_type;
	filter.flavor = selectors[0].flavor;
	ret = contents_find_programmers(&contents, &filter, images);
	if (ret < 0)
		goto out_free_contents;

	for (i = 0; i < num_selectors; i++) {
		storage_type = selectors[i].storage_type;
		flavor = selectors[i].flavor;

		op = firehose_alloc_op(FIREHOSE_OP_CONFIGURE);
		if (!op) {
			ret = -1;
			goto out_free_contents;
		}
		op->storage_type = storage_type;
		list_append(ops, &op->node);

		list_for_each_entry(entry, &contents.entries, node) {
			if (entry->file_type != CONTENTS_FILE_PROGRAM)
				continue;
			if (entry->storage_type != QDL_STORAGE_UNKNOWN && entry->storage_type != storage_type)
				continue;
			if (entry->flavor && (!flavor || strcmp(entry->flavor, flavor)))
				continue;

			filter.contents = &contents;
			filter.storage_type = storage_type;
			filter.flavor = flavor;

			ret = program_load(ops, qdl_pathbuf_str(&entry->path),
					   storage_type == QDL_STORAGE_NAND, false, &filter, incdir);
			if (ret < 0) {
				ux_err("failed to load program: %s\n", entry->filename);
				goto out_free_contents;
			}
		}

		list_for_each_entry(entry, &contents.entries, node) {
			if (entry->file_type != CONTENTS_FILE_PATCH)
				continue;
			if (entry->storage_type != QDL_STORAGE_UNKNOWN && entry->storage_type != storage_type)
				continue;
			if (entry->flavor && (!flavor || strcmp(entry->flavor, flavor)))
				continue;

			ret = patch_load(ops, qdl_pathbuf_str(&entry->path));
			if (ret < 0) {
				ux_err("failed to load %s\n", entry->filename);
				goto out_free_contents;
			}
		}
	}

out_free_contents:
	contents_free_entries(&contents);
	free(selectors);
	free(pattern_copy);

	return ret;
}

int contents_load_programmers(const char *filename, char *specifier,
			      struct sahara_image *images)
{
	struct contents_filter filter = {};
	struct contents_selector *selectors = NULL;
	struct contents_entry *entry;
	struct contents_entry *next;
	struct contents contents = {};
	size_t flavor_idx;
	int ret;

	list_init(&contents.entries);
	ret = contents_load_xml(&contents, filename);
	if (ret < 0)
		goto out_free_contents;

	ret = contents_decode_selectors(&contents, specifier, &selectors);
	if (ret < 0)
		goto out_free_contents;
	if (ret != 1) {
		ux_err("select exactly one storage/flavor combination for Sahara archive creation\n");
		ret = -1;
		goto out_free_contents;
	}

	filter.contents = &contents;
	filter.storage_type = selectors[0].storage_type;
	filter.flavor = selectors[0].flavor;
	ret = contents_find_programmers(&contents, &filter, images);

out_free_contents:
	for (flavor_idx = 0; flavor_idx < contents.num_flavors; flavor_idx++)
		free(contents.flavors[flavor_idx]);
	free(contents.flavors);

	list_for_each_entry_safe(entry, next, &contents.entries, node) {
		free(entry->filename);
		free(entry->flavor);
		free(entry);
	}
	free(selectors);

	return ret;
}

int contents_resolve_path(struct contents_filter *filter, const char *filename, struct pathbuf *path)
{
	enum qdl_storage_type storage_type;
	struct contents_entry *entry;
	struct contents *contents;
	struct pathbuf probe;
	const char *flavor;

	if (!filter)
		return 0;

	contents = filter->contents;
	storage_type = filter->storage_type;
	flavor = filter->flavor;

	/* Look for a match */
	list_for_each_entry(entry, &contents->entries, node) {
		if (storage_type != QDL_STORAGE_UNKNOWN &&
		    entry->storage_type != QDL_STORAGE_UNKNOWN &&
		    entry->storage_type != storage_type) {
			ux_debug("  storage mismatch: entry='%s' filter='%s', skipping\n",
				 contents_storage_name(entry->storage_type),
				 contents_storage_name(storage_type));
			continue;
		}
		if (entry->flavor && flavor && strcmp(entry->flavor, flavor)) {
			ux_debug("  flavor mismatch, skipping\n");
			continue;
		}

		if (strcmp(entry->filename, filename))
			continue;

		qdl_pathbuf_dup(path, &entry->path);
		return 1;
	}

	/*
	 * fh_loader adds all applicable <file_path> to the search path, to find
	 * files adjacent to those described in the contents.xml. So if we
	 * didn't find an exact match, search adjacent to all other files...
	 */
	list_for_each_entry(entry, &contents->entries, node) {
		if (storage_type != QDL_STORAGE_UNKNOWN &&
		    entry->storage_type != QDL_STORAGE_UNKNOWN &&
		    entry->storage_type != storage_type)
			continue;
		if (entry->flavor && flavor && strcmp(entry->flavor, flavor)) {
			continue;
		}

		qdl_pathbuf_dup(&probe, &entry->path);
		qdl_pathbuf_dirname(&probe);
		qdl_pathbuf_push(&probe, filename);

		if (!access(probe.buf, F_OK)) {
			qdl_pathbuf_dup(path, &probe);
			return 1;
		}
	}

	return 0;
}

/**
 * contents_filename_is_lite() - detect a "lite" firehose programmer by name
 * @filename: bare filename to check
 *
 * meta_cli's device programmer file lists carry no firehose_type the way
 * contents.xml's firehose_type="lite" attribute does, so this is inferred
 * from the filename instead. There is no portable case-insensitive
 * strstr(), so lowercase into a scratch buffer and use strstr() on that.
 *
 * Returns: true if @filename contains "lite" (case-insensitive)
 */
static bool contents_filename_is_lite(const char *filename)
{
	char lower[PATH_MAX];
	size_t len = strlen(filename);
	size_t i;

	if (len >= sizeof(lower))
		len = sizeof(lower) - 1;

	for (i = 0; i < len; i++)
		lower[i] = (char)tolower((unsigned char)filename[i]);
	lower[len] = '\0';

	return strstr(lower, "lite") != NULL;
}

/**
 * contents_populate_device_programmer_from_metacli() - get device programmer files
 * @contents: contents database to append entries to
 * @ctx: meta_cli context
 * @storage_type: storage type to query
 * @flavor: flavor to query, or NULL
 *
 * Tries get_device_programmer_config first. If it does not yield both a
 * programmer_bin and a programmer_config entry, falls back to get_files
 * for the device_programmer file type.
 *
 * Returns: 0 on success, -1 if neither call returned parseable JSON
 */
static int contents_populate_device_programmer_from_metacli(struct contents *contents, struct metacli_ctx *ctx,
						enum qdl_storage_type storage_type, const char *flavor)
{
	char storage_arg[256] = "";
	char flavor_arg[256] = "";
	char *cmd_argv_prog[4];
	int cmd_argc = 0;
	int prog_bin_count = 0;
	int prog_count = 0;
	struct json_value *prog_config = NULL;
	struct contents_entry *entry;
	bool prog_call_ok;

	cmd_argv_prog[cmd_argc++] = "get_device_programmer_config";
	snprintf(storage_arg, sizeof(storage_arg), "storage=%s", encode_storage_type(storage_type));
	cmd_argv_prog[cmd_argc++] = storage_arg;

	if (flavor) {
		snprintf(flavor_arg, sizeof(flavor_arg), "flavor=%s", flavor);
		cmd_argv_prog[cmd_argc++] = flavor_arg;
	}
	cmd_argv_prog[cmd_argc] = NULL;

	prog_call_ok = metacli_run_json(ctx, cmd_argv_prog, &prog_config) == 0;

	if (prog_call_ok) {
		struct json_value *prog_bin_array = json_get_child(prog_config, "programmer_bin");
		struct json_value *prog_array = json_get_child(prog_config, "programmer_config");

		prog_bin_count = prog_bin_array ? json_count_children(prog_bin_array) : 0;
		prog_count = prog_array ? json_count_children(prog_array) : 0;

		/*
		 * Only commit entries once we know get_device_programmer_config
		 * yielded both a programmer_bin and a programmer_config file.
		 */
		if (prog_bin_count > 0 && prog_count > 0) {
			/* Add programmer_bin files as DEVICE_PROGRAMMER entries */
			for (int p = 0; p < prog_bin_count; p++) {
				const char *prog_bin = json_get_element_string(prog_bin_array, p);
				if (prog_bin) {
#ifdef _WIN32
					char normalized[MAX_PATH];
					if (GetFullPathNameA(prog_bin, MAX_PATH, normalized, NULL)) {
						prog_bin = normalized;
					}
#endif
					char *bin_copy = strdup(prog_bin);
					const char *basename_str = basename(bin_copy);
					entry = calloc(1, sizeof(*entry));
					if (!entry) {
						free(bin_copy);
						json_free(prog_config);
						return -1;
					}
					entry->file_type = CONTENTS_FILE_DEVICE_PROGRAMMER;
					entry->storage_type = storage_type;
					if (contents_filename_is_lite(basename_str))
						entry->firehose_type = FIREHOSE_TYPE_LITE;
					if (flavor) {
						entry->flavor = strdup(flavor);
						if (!entry->flavor) {
							free(entry);
							free(bin_copy);
							json_free(prog_config);
							return -1;
						}
					}
					entry->filename = strdup(basename_str);
					free(bin_copy);
					if (!entry->filename) {
						free(entry->flavor);
						free(entry);
						json_free(prog_config);
						return -1;
					}
					qdl_pathbuf_reset(&entry->path);
#ifdef _WIN32
					strncpy(entry->path.buf, prog_bin, PATH_MAX - 1);
					entry->path.len = strlen(prog_bin);
					ux_debug("Added programmer_bin DEVICE_PROGRAMMER: filename=%s path=%s\n", entry->filename, entry->path.buf);
#else
					qdl_pathbuf_push(&entry->path, prog_bin);
					ux_debug("Added programmer_bin DEVICE_PROGRAMMER: filename=%s path=%s\n", entry->filename, qdl_pathbuf_str(&entry->path));
#endif
					list_append(&contents->entries, &entry->node);
				}
			}

			/* Add programmer_config files as PROGRAMMER_XML entries */
			for (int p = 0; p < prog_count; p++) {
				const char *prog_file = json_get_element_string(prog_array, p);
				if (prog_file) {
					entry = calloc(1, sizeof(*entry));
					if (!entry) {
						json_free(prog_config);
						return -1;
					}
					entry->file_type = CONTENTS_FILE_PROGRAMMER_XML;
					entry->storage_type = storage_type;
					if (flavor) {
						entry->flavor = strdup(flavor);
						if (!entry->flavor) {
							free(entry);
							json_free(prog_config);
							return -1;
						}
					}
					entry->filename = strdup(prog_file);
					if (!entry->filename) {
						free(entry->flavor);
						free(entry);
						json_free(prog_config);
						return -1;
					}
					qdl_pathbuf_reset(&entry->path);
					qdl_pathbuf_push(&entry->path, prog_file);
					list_append(&contents->entries, &entry->node);
				}
			}
		}
		json_free(prog_config);
	}

	if (prog_bin_count > 0 && prog_count > 0)
		return 0;

	/*
	 * get_device_programmer_config did not yield both a programmer_bin
	 * and a programmer_config entry -- fall back to get_files for the
	 * device programmer binaries.
	 */
	{
		char *cmd_argv_files[5];
		int files_argc = 0;
		struct json_value *device_programmer_files = NULL;
		int dp_count;

		cmd_argv_files[files_argc++] = "get_files";
		cmd_argv_files[files_argc++] = "file_types=['device_programmer']";
		cmd_argv_files[files_argc++] = storage_arg;

		if (flavor)
			cmd_argv_files[files_argc++] = flavor_arg;

		cmd_argv_files[files_argc] = NULL;

		if (metacli_run_json(ctx, cmd_argv_files, &device_programmer_files) != 0)
			return -1;

		dp_count = json_count_children(device_programmer_files);

		for (int p = 0; p < dp_count; p++) {
			const char *dp_file = json_get_element_string(device_programmer_files, p);

			if (dp_file) {
				const char *dp_basename = basename((char *)dp_file);

				entry = calloc(1, sizeof(*entry));
				if (!entry) {
					json_free(device_programmer_files);
					return -1;
				}
				entry->file_type = CONTENTS_FILE_DEVICE_PROGRAMMER;
				entry->storage_type = storage_type;
				if (contents_filename_is_lite(dp_basename))
					entry->firehose_type = FIREHOSE_TYPE_LITE;
				if (flavor) {
					entry->flavor = strdup(flavor);
					if (!entry->flavor) {
						free(entry);
						json_free(device_programmer_files);
						return -1;
					}
				}
				entry->filename = strdup(dp_basename);
				if (!entry->filename) {
					free(entry->flavor);
					free(entry);
					json_free(device_programmer_files);
					return -1;
				}
				qdl_pathbuf_reset(&entry->path);
#ifdef _WIN32
				strncpy(entry->path.buf, dp_file, PATH_MAX - 1);
				entry->path.len = strlen(dp_file);
#else
				qdl_pathbuf_push(&entry->path, dp_file);
#endif
				list_append(&contents->entries, &entry->node);
			}
		}
		json_free(device_programmer_files);
	}

	return 0;
}

static int contents_populate_from_metacli(struct contents *contents, struct metacli_ctx *ctx,
					   enum qdl_storage_type storage_type, const char *flavor,
					   const char *sku)
{
	struct json_value *partition_files = NULL;
	struct json_value *file_val;
	struct contents_entry *entry;
	const char *file_path;
	char flavor_arg[256] = "";
	char storage_arg[256] = "";
	char sku_arg[256] = "";
	char *cmd_argv_partition[7];
	int cmd_argc = 0;
	int file_count, k;

	/*
	 * storage_type/flavor/sku are already resolved and validated by
	 * metacli_contents_decode_selectors() before this is called, so a
	 * single get_partition_files call is all that's needed here.
	 */
	cmd_argv_partition[cmd_argc++] = "get_partition_files";
	cmd_argv_partition[cmd_argc++] = "group=True";

	if (flavor) {
		snprintf(flavor_arg, sizeof(flavor_arg), "flavor=%s", flavor);
		cmd_argv_partition[cmd_argc++] = flavor_arg;
	}

	if (sku) {
		snprintf(sku_arg, sizeof(sku_arg), "sku_config=%s", sku);
		cmd_argv_partition[cmd_argc++] = sku_arg;
	}

	snprintf(storage_arg, sizeof(storage_arg), "storage=%s", encode_storage_type(storage_type));
	cmd_argv_partition[cmd_argc++] = storage_arg;
	cmd_argv_partition[cmd_argc++] = "critical=False";
	cmd_argv_partition[cmd_argc] = NULL;

	if (metacli_run_json(ctx, cmd_argv_partition, &partition_files) < 0)
		return -1;

	/* Process partition files */
	file_val = json_get_child(partition_files, "partition");
	if (file_val) {
		file_count = json_count_children(file_val);
		for (k = 0; k < file_count; k++) {
			file_path = json_get_element_string(file_val, k);
			if (file_path) {
				entry = calloc(1, sizeof(*entry));
				if (!entry) {
					json_free(partition_files);
					return -1;
				}
				entry->file_type = CONTENTS_FILE_PROGRAM;
				entry->storage_type = storage_type;
				if (flavor) {
					entry->flavor = strdup(flavor);
					if (!entry->flavor) {
						free(entry);
						json_free(partition_files);
						return -1;
					}
				}
				entry->filename = strdup(basename((char *)file_path));
				if (!entry->filename) {
					free(entry->flavor);
					free(entry);
					json_free(partition_files);
					return -1;
				}
				qdl_pathbuf_reset(&entry->path);
#ifdef _WIN32
				strncpy(entry->path.buf, file_path, PATH_MAX - 1);
				entry->path.len = strlen(file_path);
#else
				qdl_pathbuf_push(&entry->path, file_path);
#endif
				list_append(&contents->entries, &entry->node);
			}
		}
	}

	/* Process partition_patch files */
	file_val = json_get_child(partition_files, "partition_patch");
	if (file_val) {
		file_count = json_count_children(file_val);
		for (k = 0; k < file_count; k++) {
			file_path = json_get_element_string(file_val, k);
			if (file_path) {
				entry = calloc(1, sizeof(*entry));
				if (!entry) {
					json_free(partition_files);
					return -1;
				}
				entry->file_type = CONTENTS_FILE_PATCH;
				entry->storage_type = storage_type;
				if (flavor) {
					entry->flavor = strdup(flavor);
					if (!entry->flavor) {
						free(entry);
						json_free(partition_files);
						return -1;
					}
				}
				entry->filename = strdup(file_path);
				if (!entry->filename) {
					free(entry->flavor);
					free(entry);
					json_free(partition_files);
					return -1;
				}
				qdl_pathbuf_reset(&entry->path);
#ifdef _WIN32
				strncpy(entry->path.buf, file_path, PATH_MAX - 1);
				entry->path.len = strlen(file_path);
#else
				qdl_pathbuf_push(&entry->path, file_path);
#endif
				list_append(&contents->entries, &entry->node);
			}
		}
	}

	/* Process partition_bin files (binaries referenced by
	 * <program filename="..."> inside the partition files above, e.g.
	 * persist.img referenced from a rawprogram XML). These are
	 * lookup-only entries: contents_load() never loads them directly,
	 * but contents_resolve_path() matches them by filename so
	 * program_resolve_path() can find the meta_cli-resolved absolute
	 * path instead of relying on the file sitting next to the
	 * rawprogram XML.
	 */
	file_val = json_get_child(partition_files, "partition_bin");
	if (file_val) {
		file_count = json_count_children(file_val);
		for (k = 0; k < file_count; k++) {
			file_path = json_get_element_string(file_val, k);
			if (file_path) {
				entry = calloc(1, sizeof(*entry));
				if (!entry) {
					json_free(partition_files);
					return -1;
				}
				entry->file_type = CONTENTS_FILE_OTHER;
				entry->storage_type = storage_type;
				if (flavor) {
					entry->flavor = strdup(flavor);
					if (!entry->flavor) {
						free(entry);
						json_free(partition_files);
						return -1;
					}
				}
				entry->filename = strdup(basename((char *)file_path));
				if (!entry->filename) {
					free(entry->flavor);
					free(entry);
					json_free(partition_files);
					return -1;
				}
				qdl_pathbuf_reset(&entry->path);
#ifdef _WIN32
				strncpy(entry->path.buf, file_path, PATH_MAX - 1);
				entry->path.len = strlen(file_path);
#else
				qdl_pathbuf_push(&entry->path, file_path);
#endif
				list_append(&contents->entries, &entry->node);
			}
		}
	}

	json_free(partition_files);

	/* Get device programmer files for the selected storage/flavor */
	if (contents_populate_device_programmer_from_metacli(contents, ctx, storage_type, flavor) < 0)
		return -1;

	return 0;
}
