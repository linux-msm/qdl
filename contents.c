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
#include "qdl.h"

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

struct contents_filter {
	struct contents *contents;

	enum qdl_storage_type storage_type;
	const char *flavor;
};

struct contents_selector {
	enum qdl_storage_type storage_type;
	const char *flavor;
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

		if (!xmlStrcmp(node->name, (xmlChar *)"linux_root_path")) {
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

static int contents_find_programmers(struct contents *contents, struct sahara_image *images)
{
	struct contents_filter filter = { .contents = contents };
	struct contents_entry *entry;
	struct sahara_image blob;
	int ret;

	list_for_each_entry(entry, &contents->entries, node) {
		if (entry->file_type != CONTENTS_FILE_PROGRAMMER_XML)
			continue;

		ret = load_sahara_image(NULL, qdl_pathbuf_str(&entry->path), &blob);
		if (ret < 0) {
			ux_err("unable to open \"%s\" for reading\n", qdl_pathbuf_str(&entry->path));
			continue;
		}

		ret = decode_sahara_config(&blob, images, &filter);
		if (ret == 0) {
			ux_err("%s is not a programmer xml\n", qdl_pathbuf_str(&entry->path));
			sahara_images_free(&blob, 1);
			continue;
		} else if (ret < 0) {
			ux_err("failed to parse programmer xml \"%s\"\n", qdl_pathbuf_str(&entry->path));
			return -1;
		}

		return 0;
	}

	list_for_each_entry(entry, &contents->entries, node) {
		if (entry->file_type != CONTENTS_FILE_DEVICE_PROGRAMMER)
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

		if (strchr(flavor, '/')) {
			ux_err("invalid flavor selector \"%s\"\n", flavor);
			goto err_free_selectors;
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

int contents_load(struct list_head *ops, const char *filename, char *specifier,
		  struct sahara_image *images, const char *incdir)
{
	struct contents_filter filter = {};
	struct contents_entry *entry;
	struct contents_entry *next;
	struct contents contents = {};
	enum qdl_storage_type storage_type;
	struct contents_selector *selectors = NULL;
	struct firehose_op *op;
	const char *flavor;
	int num_selectors;
	char *pattern = specifier;
	size_t flavor_idx;
	int ret;
	int i;

	list_init(&contents.entries);
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

	ret = contents_find_programmers(&contents, images);
	if (ret < 0)
		goto out_free_contents;

	for (i = 0; i < num_selectors; i++) {
		storage_type = selectors[i].storage_type;
		flavor = selectors[i].flavor;

		op = firehose_alloc_op(FIREHOSE_OP_CONFIGURE);
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
		if (entry->storage_type != QDL_STORAGE_UNKNOWN && entry->storage_type != storage_type) {
			continue;
		}
		if (entry->flavor && flavor && strcmp(entry->flavor, flavor)) {
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
		if (entry->storage_type != QDL_STORAGE_UNKNOWN && entry->storage_type != storage_type) {
			continue;
		}
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
