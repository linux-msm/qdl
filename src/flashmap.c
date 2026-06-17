// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "file.h"
#include "json.h"
#include "list.h"
#include "firehose.h"
#include "qdl.h"

enum {
	QDL_FILE_UNKNOWN,
	QDL_FILE_PATCH,
	QDL_FILE_PROGRAM,
};

static int flashmap_resolve_path(char *path, size_t path_size, const char *filename, const char *incdir)
{
	if (!filename) {
		ux_err("flashmap: filename is null\n");
		return -1;
	}

	if (incdir) {
		snprintf(path, path_size, "%s/%s", incdir, filename);
		if (access(path, F_OK))
			snprintf(path, path_size, "%s", filename);
	} else {
		snprintf(path, path_size, "%s", filename);
	}

	return 0;
}

static int flashmap_get_legacy_programmer(struct qdl_zip *zip, struct json_value *layout,
					  struct sahara_image *images, const char *incdir)
{
	struct json_value *programmers;
	const char *filename;
	char path[PATH_MAX];
	int count;

	programmers = json_get_child(layout, "programmer");
	if (!programmers) {
		ux_err("flashmap: parse error when decoding programmer\n");
		return -1;
	}

	count = json_count_children(programmers);
	if (count != 1) {
		ux_err("flashmap: single programmer expected, found %d\n", count);
		return -1;
	}

	filename = json_get_element_string(programmers, 0);
	if (!filename) {
		ux_err("flashmap: parse error when decoding programmer\n");
		return -1;
	}

	if (flashmap_resolve_path(path, sizeof(path), filename, incdir))
		return -1;

	ux_debug("flashmap: selected programmer: %s\n", path);

	return load_sahara_image(zip, path, &images[SAHARA_ID_EHOSTDL_IMG]);
}

static int flashmap_get_programmer_map(struct qdl_zip *zip, struct json_value *layout,
				       struct sahara_image *images, const char *incdir)
{
	struct json_value *programmers;
	struct json_value *entry;
	unsigned long image_id;
	const char *filename;
	char *end;
	char path[PATH_MAX];
	int count = 0;
	int ret;

	programmers = json_get_child(layout, "programmer");
	if (!programmers || programmers->type != JSON_TYPE_OBJECT) {
		ux_err("flashmap: programmer map must be an object for version 1.2.0-qdl\n");
		return -1;
	}

	for (entry = programmers->u.value; entry; entry = entry->next) {
		errno = 0;
		image_id = strtoul(entry->key, &end, 0);
		if (errno || end == entry->key || *end || image_id == 0 || image_id >= MAPPING_SZ) {
			ux_err("flashmap: invalid programmer image id \"%s\"\n", entry->key);
			return -1;
		}

		if (entry->type != JSON_TYPE_STRING || !entry->u.string) {
			ux_err("flashmap: programmer entry \"%s\" must be a filename string\n", entry->key);
			return -1;
		}

		filename = entry->u.string;
		ret = flashmap_resolve_path(path, sizeof(path), filename, incdir);
		if (ret)
			return ret;

		ux_debug("flashmap: selected programmer %lu: %s\n", image_id, path);

		ret = load_sahara_image(zip, path, &images[image_id]);
		if (ret)
			return ret;

		count++;
	}

	if (!count) {
		ux_err("flashmap: programmer map is empty\n");
		return -1;
	}

	return 0;
}

static int flashmap_get_programmers(struct qdl_zip *zip, struct json_value *layout,
				    struct sahara_image *images, const char *incdir,
				    bool uses_programmer_map)
{
	if (uses_programmer_map)
		return flashmap_get_programmer_map(zip, layout, images, incdir);

	return flashmap_get_legacy_programmer(zip, layout, images, incdir);
}

static int flashmap_load_xml(struct list_head *ops, struct qdl_zip *zip, const char *filename,
			     bool is_nand, const char *incdir)
{
	struct qdl_file file;
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;
	size_t len;
	void *xml;
	int type = QDL_FILE_UNKNOWN;
	int ret;

	ret = qdl_file_open(zip, filename, &file);
	if (ret < 0) {
		ux_err("unable to parse XML: %s\n", filename);
		return -1;
	}

	xml = qdl_file_load(&file, &len);
	if (!xml) {
		ux_err("failed to load \"%s\"\n", filename);
		ret = -1;
		goto out_close_file;
	}

	if (len > INT_MAX) {
		ux_err("\"%s\" too large for XML parser\n", filename);
		ret = -1;
		goto out_free_xml;
	}

	doc = xmlReadMemory(xml, (int)len, filename, NULL, 0);
	if (!doc) {
		ux_err("failed to parse %s\n", filename);
		ret = -1;
		goto out_free_xml;
	}

	root = xmlDocGetRootElement(doc);
	if (!root) {
		ux_err("unable to find XML root in \"%s\"\n", filename);
		ret = -1;
		goto out_free_doc;
	}

	if (!xmlStrcmp(root->name, (xmlChar *)"patches")) {
		type = QDL_FILE_PATCH;
	} else if (!xmlStrcmp(root->name, (xmlChar *)"data")) {
		type = QDL_FILE_PROGRAM;
		for (node = root->children; node ; node = node->next) {
			if (node->type != XML_ELEMENT_NODE)
				continue;
			if (!xmlStrcmp(node->name, (xmlChar *)"program") ||
			    !xmlStrcmp(node->name, (xmlChar *)"erase")) {
				type = QDL_FILE_PROGRAM;
				break;
			}
		}
	}

	switch (type) {
	case QDL_FILE_PROGRAM:
		ret = program_load_xml(ops, doc, zip, filename, is_nand, false, NULL, incdir);
		break;
	case QDL_FILE_PATCH:
		ret = patch_load_xml(ops, doc, filename);
		break;
	default:
		ux_err("unknown file type: %s\n", filename);
		ret = -1;
		break;
	}

out_free_doc:
	xmlFreeDoc(doc);
out_free_xml:
	free(xml);
out_close_file:
	qdl_file_close(&file);

	return ret;
}

static int flashmap_enumerate_programmables(struct json_value *list, struct list_head *ops,
					    struct qdl_zip *zip, const char *incdir)
{
	struct json_value *programmable;
	struct json_value *files;
	struct firehose_op *op;
	char path[PATH_MAX];
	const char *memory;
	const char *file;
	int file_count;
	int file_idx;
	bool is_nand;
	double slot;
	int count;
	int ret;
	int i;

	count = json_count_children(list);
	if (count < 0) {
		ux_err("flashmap: programmable list is not an array\n");
		return -1;
	}
	if (count == 0) {
		ux_err("flashmap: programmable list is empty\n");
		return -1;
	}

	for (i = 0; i < count; i++) {
		programmable = json_get_element_object(list, i);

		memory = json_get_string(programmable, "memory");
		ret = json_get_number(programmable, "slot", &slot);
		files = json_get_child(programmable, "files");

		if (!memory || ret < 0 || !files) {
			ux_err("failed to parse flashmap\n");
			return -1;
		}

		is_nand = !strcmp(memory, "nand");

		op = firehose_alloc_op(FIREHOSE_OP_CONFIGURE);
		op->storage_type = decode_storage_type(memory);
		list_append(ops, &op->node);

		file_count = json_count_children(files);
		if (file_count < 0) {
			ux_err("flashmap: files list is not an array\n");
			return -1;
		}

		for (file_idx = 0; file_idx < file_count; file_idx++) {
			file = json_get_element_string(files, file_idx);
			if (!file) {
				ux_err("flashmap: parse error when decoding files\n");
				return -1;
			}

			if (flashmap_resolve_path(path, sizeof(path), file, incdir))
				return -1;

			ret = flashmap_load_xml(ops, zip, path, is_nand, incdir);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int flashmap_decode_storage_filter(char *filter, unsigned int *type_filter)
{
	enum qdl_storage_type type;
	char *save = NULL;
	char *tmp;

	for (tmp = strtok_r(filter, ",", &save); tmp; tmp = strtok_r(NULL, ",", &save)) {
		type = decode_storage_type(tmp);
		if (type == QDL_STORAGE_UNKNOWN) {
			ux_err("unknown storage type \"%s\"\n", tmp);
			return -1;
		}

		*type_filter |= 1U << type;
	}

	return 0;
}

static int flashmap_decode_specifier(char *specifier, unsigned int *type_filter,
				     char **layout_selector)
{
	char *storage_filter;
	char *slash;

	*type_filter = 0;
	*layout_selector = NULL;

	if (!specifier)
		return 0;

	slash = strchr(specifier, '/');
	if (slash) {
		*slash = '\0';
		storage_filter = slash + 1;

		if (!specifier[0]) {
			ux_err("empty flashmap layout selector\n");
			return -1;
		}
		if (!storage_filter[0]) {
			ux_err("empty flashmap storage selector\n");
			return -1;
		}

		*layout_selector = specifier;
		return flashmap_decode_storage_filter(storage_filter, type_filter);
	}

	if (decode_storage_type(specifier) != QDL_STORAGE_UNKNOWN || strchr(specifier, ','))
		return flashmap_decode_storage_filter(specifier, type_filter);

	*layout_selector = specifier;

	return 0;
}

static void flashmap_print_available_layouts(struct json_value *layouts)
{
	struct json_value *layout;
	const char *name;
	int count;
	int i;

	count = json_count_children(layouts);
	if (count <= 0)
		return;

	ux_err("flashmap: available layouts:\n");
	for (i = 0; i < count; i++) {
		layout = json_get_element_object(layouts, i);
		name = json_get_string(layout, "name");
		if (name)
			ux_err("flashmap:   %s\n", name);
	}
}

static struct json_value *flashmap_select_layout(struct json_value *layouts, const char *selector)
{
	struct json_value *layout;
	const char *name;
	int count;
	int i;

	count = json_count_children(layouts);
	if (count < 0) {
		ux_err("flashmap: layouts is not an array\n");
		return NULL;
	}
	if (!count) {
		ux_err("flashmap: layouts list is empty\n");
		return NULL;
	}

	if (!selector) {
		if (count == 1)
			return json_get_element_object(layouts, 0);

		ux_err("flashmap: multiple layouts found, select one with ::<layout> or ::<layout>/<storage>[,<storage>...]\n");
		flashmap_print_available_layouts(layouts);
		return NULL;
	}

	for (i = 0; i < count; i++) {
		layout = json_get_element_object(layouts, i);
		name = json_get_string(layout, "name");
		if (name && !strcmp(name, selector)) {
			ux_debug("flashmap: selected layout: %s\n", name);
			return layout;
		}
	}

	ux_err("flashmap: layout \"%s\" not found\n", selector);
	flashmap_print_available_layouts(layouts);

	return NULL;
}

int flashmap_load(struct list_head *ops, const char *filename, char *specifier,
		  struct sahara_image *images, const char *incdir)
{
	struct list_head flashmap_ops = LIST_INIT(flashmap_ops);
	enum qdl_storage_type current_type = QDL_STORAGE_UNKNOWN;
	struct json_value *programmable;
	struct json_value *product;
	struct json_value *layout;
	struct firehose_op *op;
	struct firehose_op *next;
	struct qdl_file flashmap;
	struct json_value *json;
	struct json_value *obj;
	struct qdl_zip *zip;
	unsigned int type_filter = 0;
	unsigned int matched_ops = 0;
	bool uses_programmer_map = false;
	char *layout_selector;
	const char *version;
	const char *name;
	size_t json_size;
	void *json_blob;
	int count;
	int ret = -1;

	ret = flashmap_decode_specifier(specifier, &type_filter, &layout_selector);
	if (ret)
		return ret;

	if (!type_filter)
		type_filter = ~0U;

	ret = qdl_zip_open(filename, &zip);
	if (ret < 0) {
		ux_err("unable to create zip reference\n");
		return -1;
	}

	ret = qdl_file_open(zip, zip ? "flashmap.json" : filename, &flashmap);
	if (ret < 0) {
		ux_err("failed to open flashmap\n");
		goto out_put_zip;
	}

	json_blob = qdl_file_load(&flashmap, &json_size);
	qdl_file_close(&flashmap);
	if (!json_blob)
		goto out_put_zip;

	json = json_parse_buf(json_blob, json_size);
	if (!json) {
		ux_err("failed to parse flashmap json\n");
		goto out_free_blob;
	}

	version = json_get_string(json, "version");
	if (!version) {
		ux_err("unsupported flashmap version\n");
		ret = -1;
		goto out_free_json;
	}

	if (!strcmp(version, "1.2.0-qdl"))
		uses_programmer_map = true;
	else if (strcmp(version, "1.1.0")) {
		ux_err("unsupported flashmap version\n");
		ret = -1;
		goto out_free_json;
	}

	ux_debug("found flashmap of version: %s\n", version);

	obj = json_get_child(json, "products");

	count = json_count_children(obj);
	if (count != 1) {
		ux_err("flashmap: only single product map supported, found %d\n", count);
		ret = -1;
		goto out_free_json;
	}

	product = json_get_element_object(obj, 0);

	name = json_get_string(product, "name");
	ux_debug("product: %s\n", name);

	obj = json_get_child(product, "layouts");

	layout = flashmap_select_layout(obj, layout_selector);
	if (!layout) {
		ret = -1;
		goto out_free_json;
	}

	ret = flashmap_get_programmers(zip, layout, images, zip ? NULL : incdir, uses_programmer_map);
	if (ret)
		goto out_free_json;

	programmable = json_get_child(layout, "programmable");
	if (!programmable) {
		ux_err("flashmap: parse error when decoding programmables\n");
		sahara_images_free(images, MAPPING_SZ);
		ret = -1;
		goto out_free_json;
	}

	ret = flashmap_enumerate_programmables(programmable, &flashmap_ops, zip, zip ? NULL : incdir);
	if (ret < 0) {
		firehose_free_ops(&flashmap_ops);
		sahara_images_free(images, MAPPING_SZ);
		goto out_free_json;
	}

	list_for_each_entry_safe(op, next, &flashmap_ops, node) {
		if (op->storage_type != QDL_STORAGE_UNKNOWN)
			current_type = op->storage_type;

		if ((1U << current_type) & type_filter) {
			list_del(&op->node);
			list_append(ops, &op->node);
			matched_ops++;
		}
	}

	firehose_free_ops(&flashmap_ops);

	if (!matched_ops) {
		ux_err("loaded flashmap does not contain any operations for selected storage type\n");
		ret = -1;
	}

out_free_json:
	json_free(json);
out_free_blob:
	free(json_blob);
out_put_zip:
	qdl_zip_put(zip);

	return ret;
}
