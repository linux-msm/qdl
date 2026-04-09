// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int flashmap_get_programmers(struct qdl_zip *zip, struct json_value *layout,
				    struct sahara_image *images, const char *incdir)
{
	struct json_value *programmers;
	const char *filename;
	char path[PATH_MAX];
	int count;

	programmers = json_get_child(layout, "programmer");
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

	if (incdir) {
		snprintf(path, PATH_MAX, "%s/%s", incdir, filename);
		if (access(path, F_OK))
			snprintf(path, PATH_MAX, "%s", filename);
	} else {
		snprintf(path, PATH_MAX, "%s", filename);
	}

	ux_debug("flashmap: selected programmer: %s\n", path);

	return load_sahara_image(zip, path, &images[SAHARA_ID_EHOSTDL_IMG]);
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
	if (len > INT_MAX) {
		ux_err("XML too large: %s\n", filename);
		free(xml);
		return -1;
	}

	doc = xmlReadMemory(xml, (int)len, filename, NULL, 0);

	root = xmlDocGetRootElement(doc);
	if (!root) {
		ux_err("unable to find XML root in %s\n", filename);
		xmlFreeDoc(doc);
		return -1;
	}

	if (!xmlStrcmp(root->name, (xmlChar *)"patches")) {
		type = QDL_FILE_PATCH;
	} else if (!xmlStrcmp(root->name, (xmlChar *)"data")) {
		for (node = root->children; node ; node = node->next) {
			if (node->type != XML_ELEMENT_NODE)
				continue;
			if (!xmlStrcmp(node->name, (xmlChar *)"program")) {
				type = QDL_FILE_PROGRAM;
				break;
			}
		}
	}

	switch (type) {
	case QDL_FILE_PROGRAM:
		ret = program_load_xml(ops, doc, zip, filename, is_nand, false, incdir);
		break;
	case QDL_FILE_PATCH:
		ret = patch_load_xml(ops, doc, filename);
		break;
	default:
		ux_err("unknown file type: %s\n", filename);
		ret = -1;
		break;
	}

	xmlFreeDoc(doc);
	free(xml);
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
		op->storage_type = decode_storage(memory);
		list_append(ops, &op->node);

		file_count = json_count_children(files);
		for (file_idx = 0; file_idx < file_count; file_idx++) {
			file = json_get_element_string(files, file_idx);
			if (incdir) {
				snprintf(path, PATH_MAX, "%s/%s", incdir, file);
				if (access(path, F_OK))
					snprintf(path, PATH_MAX, "%s", file);
			} else {
				snprintf(path, PATH_MAX, "%s", file);
			}

			ret = flashmap_load_xml(ops, zip, path, is_nand, incdir);
			if (ret)
				return ret;
		}
	}

	return 0;
}

int flashmap_load(struct list_head *ops, const char *filename, struct sahara_image *images, const char *incdir)
{
	struct json_value *programmable;
	struct json_value *product;
	struct json_value *layout;
	struct qdl_file flashmap;
	struct json_value *json;
	struct json_value *obj;
	struct qdl_zip *zip;
	const char *version;
	const char *name;
	size_t json_size;
	void *json_blob;
	int count;
	int ret;

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
	if (!version || strcmp(version, "1.1.0")) {
		ux_err("unsupported flashmap version\n");
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

	count = json_count_children(obj);
	if (count != 1) {
		ux_err("flashmap: only one layout supported, found %d\n", count);
		ret = -1;
		goto out_free_json;
	}

	layout = json_get_element_object(obj, 0);

	ret = flashmap_get_programmers(zip, layout, images, zip ? NULL : incdir);
	if (ret)
		goto out_free_json;

	programmable = json_get_child(layout, "programmable");
	if (!programmable) {
		ux_err("flashmap: parse error when decoding programmables\n");
		sahara_images_free(images, MAPPING_SZ);
		ret = -1;
		goto out_free_json;
	}

	ret = flashmap_enumerate_programmables(programmable, ops, zip, zip ? NULL : incdir);
	if (ret < 0) {
		firehose_free_ops(ops);
		sahara_images_free(images, MAPPING_SZ);
	}

out_free_json:
	json_free(json);
out_free_blob:
	free(json_blob);
out_put_zip:
	qdl_zip_put(zip);

	return ret;
}
