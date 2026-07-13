// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <limits.h>
#include <libgen.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zip.h>

#include "flashmap.h"
#include "firehose.h"
#include "list.h"
#include "qdl.h"
#include "sparse.h"

static char *zipper_basename(const char *path)
{
	char *tmp;
	char *base;

	if (!path || !path[0])
		return NULL;

	tmp = strdup(path);
	if (!tmp)
		return NULL;

	base = basename(tmp);
	base = base ? strdup(base) : NULL;
	free(tmp);

	return base;
}

static void xml_setpropf(xmlNode *node, const char *attr, const char *fmt, ...)
{
	char *buf;
	va_list ap;

	if (!node || !attr || !fmt)
		return;

	va_start(ap, fmt);
	if (vasprintf(&buf, fmt, ap) < 0) {
		va_end(ap);
		return;
	}
	va_end(ap);

	xmlSetProp(node, (xmlChar *)attr, (xmlChar *)buf);
	free(buf);
}

struct zipper_name_entry {
	char *name;
	struct list_head node;
};

struct zipper_file_rename {
	enum qdl_storage_type storage_type;
	char *source_name;
	char *zip_name;
	struct list_head node;
};

struct zipper_storage_ctx {
	enum qdl_storage_type storage_type;
	const char *memory;
	xmlDoc *program_doc;
	xmlNode *program_root;
	xmlDoc *patch_doc;
	xmlNode *patch_root;
	char *program_xml_name;
	char *patch_xml_name;
	struct list_head node;
};

struct flashmap_json_buf {
	char *buf;
	size_t len;
	size_t cap;
	bool error;
};

static void zipper_free_name_entries(struct list_head *names)
{
	struct zipper_name_entry *entry;
	struct zipper_name_entry *next;

	list_for_each_entry_safe(entry, next, names, node) {
		list_del(&entry->node);
		free(entry->name);
		free(entry);
	}
}

static bool zipper_name_is_used(struct list_head *names, const char *name)
{
	struct zipper_name_entry *entry;

	list_for_each_entry(entry, names, node) {
		if (!strcmp(entry->name, name))
			return true;
	}

	return false;
}

static int zipper_add_name_entry(struct list_head *names, const char *name)
{
	struct zipper_name_entry *entry;

	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return -1;

	entry->name = strdup(name);
	if (!entry->name) {
		free(entry);
		return -1;
	}

	list_append(names, &entry->node);

	return 0;
}

/* Takes ownership of "data"; the buffer is freed on error as well. */
static int zipper_add_buffer_owned(zip_t *zip, const char *name, void *data, size_t len)
{
	zip_source_t *source;
	zip_int64_t idx;

	source = zip_source_buffer(zip, data, len, 1);
	if (!source) {
		ux_err("failed to create zip source for \"%s\"\n", name);
		free(data);
		return -1;
	}

	idx = zip_file_add(zip, name, source, ZIP_FL_OVERWRITE);
	if (idx < 0) {
		ux_err("failed to add \"%s\" to zip\n", name);
		zip_source_free(source);
		return -1;
	}

	return 0;
}

static int zipper_add_buffer(zip_t *zip, const char *name, const void *data, size_t len)
{
	void *copy = NULL;

	if (len) {
		copy = malloc(len);
		if (!copy) {
			ux_err("failed to allocate zip member \"%s\"\n", name);
			return -1;
		}
		memcpy(copy, data, len);
	}

	return zipper_add_buffer_owned(zip, name, copy, len);
}

static int zipper_add_file(zip_t *zip, const char *name, const char *path)
{
	zip_source_t *source;
	zip_int64_t idx;

	source = zip_source_file(zip, path, 0, -1);
	if (!source) {
		ux_err("failed to create zip file source for \"%s\"\n", path);
		return -1;
	}

	idx = zip_file_add(zip, name, source, ZIP_FL_OVERWRITE);
	if (idx < 0) {
		ux_err("failed to add \"%s\" to zip\n", name);
		zip_source_free(source);
		return -1;
	}

	return 0;
}

static void zipper_close_progress_cb(zip_t *archive __unused,
				     double progress,
				     void *userdata)
{
	unsigned int value;
	const char *label = userdata ? (const char *)userdata : "finalize";

	if (progress < 0.0)
		progress = 0.0;
	else if (progress > 1.0)
		progress = 1.0;

	value = (unsigned int)(progress * 100000.0);
	ux_progress(label, value, 100000);
}

static struct zipper_file_rename *zipper_find_file_rename(struct list_head *renames,
							  enum qdl_storage_type storage_type,
							  const char *source_name)
{
	struct zipper_file_rename *rename;

	list_for_each_entry(rename, renames, node) {
		if (rename->storage_type == storage_type &&
		    !strcmp(rename->source_name, source_name))
			return rename;
	}

	return NULL;
}

static void zipper_free_file_renames(struct list_head *renames)
{
	struct zipper_file_rename *rename;
	struct zipper_file_rename *next;

	list_for_each_entry_safe(rename, next, renames, node) {
		list_del(&rename->node);
		free(rename->source_name);
		free(rename->zip_name);
		free(rename);
	}
}

static struct zipper_storage_ctx *zipper_find_storage_ctx(struct list_head *storages,
							  enum qdl_storage_type storage_type)
{
	struct zipper_storage_ctx *storage;

	list_for_each_entry(storage, storages, node) {
		if (storage->storage_type == storage_type)
			return storage;
	}

	return NULL;
}

static void zipper_free_storage_contexts(struct list_head *storages)
{
	struct zipper_storage_ctx *storage;
	struct zipper_storage_ctx *next;

	list_for_each_entry_safe(storage, next, storages, node) {
		list_del(&storage->node);
		xmlFreeDoc(storage->program_doc);
		xmlFreeDoc(storage->patch_doc);
		free(storage->program_xml_name);
		free(storage->patch_xml_name);
		free(storage);
	}
}

static struct zipper_storage_ctx *zipper_create_storage_ctx(enum qdl_storage_type storage_type)
{
	struct zipper_storage_ctx *storage;
	const char *memory;

	memory = encode_storage_type(storage_type);
	if (!memory) {
		ux_err("unknown storage type %d\n", storage_type);
		return NULL;
	}

	storage = calloc(1, sizeof(*storage));
	if (!storage)
		return NULL;

	storage->storage_type = storage_type;
	storage->memory = memory;

	storage->program_doc = xmlNewDoc((xmlChar *)"1.0");
	storage->program_root = xmlNewNode(NULL, (xmlChar *)"data");
	if (!storage->program_doc || !storage->program_root)
		goto err;
	xmlDocSetRootElement(storage->program_doc, storage->program_root);

	storage->patch_doc = xmlNewDoc((xmlChar *)"1.0");
	storage->patch_root = xmlNewNode(NULL, (xmlChar *)"patches");
	if (!storage->patch_doc || !storage->patch_root)
		goto err;
	xmlDocSetRootElement(storage->patch_doc, storage->patch_root);

	return storage;

err:
	xmlFreeDoc(storage->program_doc);
	xmlFreeDoc(storage->patch_doc);
	free(storage);
	return NULL;
}

static int zipper_storage_append_program(struct zipper_storage_ctx *storage, struct firehose_op *op,
					 const char *filename, unsigned int file_offset)
{
	xmlNode *node;

	if (!filename)
		return 0;

	node = xmlNewChild(storage->program_root, NULL, (xmlChar *)"program", NULL);
	if (!node)
		return -1;

	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%u", op->sector_size);
	xml_setpropf(node, "filename", "%s", filename);
	xml_setpropf(node, "num_partition_sectors", "%u", op->num_sectors);
	xml_setpropf(node, "physical_partition_number", "%d", op->partition);
	xml_setpropf(node, "start_sector", "%s", op->start_sector ? op->start_sector : "0");

	if (op->label)
		xml_setpropf(node, "label", "%s", op->label);

	if (op->is_nand) {
		xml_setpropf(node, "PAGES_PER_BLOCK", "%u", op->pages_per_block);
		xml_setpropf(node, "last_sector", "%u", op->last_sector);
	} else {
		xml_setpropf(node, "file_sector_offset", "%u", file_offset);
	}

	return 0;
}

static int zipper_register_raw_program_file(zip_t *zip, struct list_head *name_entries,
					    const char *memory, struct firehose_op *op,
					    unsigned int *raw_idx, char **zip_name)
{
	zip_source_t *source;
	zip_int64_t idx;
	uint64_t raw_size;
	char *filename;
	int ret;

	if (!op->sector_size || !op->num_sectors || op->sparse_offset < 0) {
		ux_err("invalid sparse raw chunk parameters\n");
		return -1;
	}

	if ((uint64_t)op->num_sectors > INT64_MAX / op->sector_size) {
		ux_err("sparse raw chunk too large\n");
		return -1;
	}
	raw_size = (uint64_t)op->num_sectors * op->sector_size;

	filename = calloc(1, strlen(memory) + 31);
	if (!filename)
		return -1;

	snprintf(filename, strlen(memory) + 31, "%s_raw_%06u.bin", memory, (*raw_idx)++);
	if (zipper_name_is_used(name_entries, filename)) {
		ux_err("duplicate generated raw filename \"%s\"\n", filename);
		goto err_free_filename;
	}

	source = zip_source_file(zip, op->filename, (zip_uint64_t)op->sparse_offset,
				 (zip_int64_t)raw_size);
	if (!source) {
		ux_err("failed to create zip file source for \"%s\"\n", op->filename);
		goto err_free_filename;
	}

	idx = zip_file_add(zip, filename, source, ZIP_FL_OVERWRITE);
	if (idx < 0) {
		ux_err("failed to add \"%s\" to zip\n", filename);
		goto err_free_zip_source;
	}

	ret = zipper_add_name_entry(name_entries, filename);
	if (ret) {
		/* Don't free "source", it's owned by libzip since zip_file_add() */
		goto err_free_filename;
	}

	*zip_name = filename;
	return 0;

err_free_zip_source:
	zip_source_free(source);
err_free_filename:
	free(filename);
	return -1;
}

static int zipper_register_fill_program_file(zip_t *zip, struct list_head *name_entries,
					     const char *memory, struct firehose_op *op,
					     unsigned int *fill_idx, char **zip_name)
{
	uint32_t fill_value = op->sparse_fill_value;
	size_t fill_size;
	char *filename;
	void *buf;
	size_t i;
	int ret;

	if (!op->sector_size || !op->num_sectors) {
		ux_err("invalid sparse fill chunk parameters\n");
		return -1;
	}

	if ((size_t)op->num_sectors > SIZE_MAX / op->sector_size) {
		ux_err("sparse fill chunk too large\n");
		return -1;
	}
	fill_size = (size_t)op->num_sectors * op->sector_size;

	filename = calloc(1, strlen(memory) + 32);
	if (!filename)
		return -1;

	snprintf(filename, strlen(memory) + 32, "%s_fill_%06u.bin", memory, (*fill_idx)++);
	if (zipper_name_is_used(name_entries, filename)) {
		ux_err("duplicate generated fill filename \"%s\"\n", filename);
		free(filename);
		return -1;
	}

	buf = malloc(fill_size);
	if (!buf) {
		free(filename);
		return -1;
	}

	for (i = 0; i + sizeof(fill_value) <= fill_size; i += sizeof(fill_value))
		memcpy((char *)buf + i, &fill_value, sizeof(fill_value));
	while (i < fill_size) {
		((char *)buf)[i] = ((char *)&fill_value)[i % sizeof(fill_value)];
		i++;
	}

	ret = zipper_add_buffer_owned(zip, filename, buf, fill_size);
	if (ret) {
		free(filename);
		return -1;
	}

	ret = zipper_add_name_entry(name_entries, filename);
	if (ret) {
		free(filename);
		return -1;
	}

	*zip_name = filename;
	return 0;
}

static int zipper_register_program_file(zip_t *zip, struct list_head *name_entries,
					struct list_head *renames,
					enum qdl_storage_type storage_type, const char *memory,
					const char *source_name, const char **zip_name)
{
	struct zipper_file_rename *rename;
	char *file_basename;
	char *renamed_file;
	int ret;

	rename = zipper_find_file_rename(renames, storage_type, source_name);
	if (rename) {
		*zip_name = rename->zip_name;
		return 0;
	}

	file_basename = zipper_basename(source_name);
	if (!file_basename) {
		ux_err("failed to parse basename for \"%s\"\n", source_name);
		return -1;
	}

	if (asprintf(&renamed_file, "%s_%s", memory, file_basename) < 0) {
		free(file_basename);
		return -1;
	}
	free(file_basename);

	if (zipper_name_is_used(name_entries, renamed_file)) {
		ux_err("duplicate zip filename \"%s\"\n", renamed_file);
		free(renamed_file);
		return -1;
	}

	ret = zipper_add_file(zip, renamed_file, source_name);
	if (ret) {
		ux_err("failed to copy \"%s\"\n", source_name);
		free(renamed_file);
		return -1;
	}

	ret = zipper_add_name_entry(name_entries, renamed_file);
	if (ret) {
		free(renamed_file);
		return -1;
	}

	rename = calloc(1, sizeof(*rename));
	if (!rename) {
		free(renamed_file);
		return -1;
	}

	rename->storage_type = storage_type;
	rename->source_name = strdup(source_name);
	rename->zip_name = renamed_file;
	if (!rename->source_name) {
		free(rename->zip_name);
		free(rename);
		return -1;
	}

	list_append(renames, &rename->node);
	*zip_name = rename->zip_name;

	return 0;
}

static int zipper_storage_append_erase(struct zipper_storage_ctx *storage, struct firehose_op *op)
{
	xmlNode *node;

	node = xmlNewChild(storage->program_root, NULL, (xmlChar *)"erase", NULL);
	if (!node)
		return -1;

	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%u", op->sector_size);
	xml_setpropf(node, "num_partition_sectors", "%u", op->num_sectors);
	xml_setpropf(node, "physical_partition_number", "%d", op->partition);
	xml_setpropf(node, "start_sector", "%s", op->start_sector ? op->start_sector : "0");

	if (op->is_nand)
		xml_setpropf(node, "PAGES_PER_BLOCK", "%u", op->pages_per_block);

	return 0;
}

static int zipper_storage_append_patch(struct zipper_storage_ctx *storage, struct firehose_op *op)
{
	xmlNode *node;

	node = xmlNewChild(storage->patch_root, NULL, (xmlChar *)"patch", NULL);
	if (!node)
		return -1;

	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%u", op->sector_size);
	xml_setpropf(node, "byte_offset", "%u", op->byte_offset);
	xml_setpropf(node, "filename", "%s", op->filename ? op->filename : "");
	xml_setpropf(node, "physical_partition_number", "%d", op->partition);
	xml_setpropf(node, "size_in_bytes", "%u", op->size_in_bytes);
	xml_setpropf(node, "start_sector", "%s", op->start_sector ? op->start_sector : "0");
	xml_setpropf(node, "value", "%s", op->value ? op->value : "");
	xml_setpropf(node, "what", "%s", op->what ? op->what : "");

	return 0;
}

static void flashmap_json_grow(struct flashmap_json_buf *json, size_t extra)
{
	size_t needed;
	size_t next_cap;
	char *new_buf;

	if (json->error)
		return;

	needed = json->len + extra + 1;
	if (needed <= json->cap)
		return;

	next_cap = json->cap ? json->cap : 256;
	while (next_cap < needed) {
		if (next_cap > SIZE_MAX / 2) {
			json->error = true;
			return;
		}
		next_cap *= 2;
	}

	new_buf = realloc(json->buf, next_cap);
	if (!new_buf) {
		json->error = true;
		return;
	}

	json->buf = new_buf;
	json->cap = next_cap;
}

static void flashmap_json_append_raw(struct flashmap_json_buf *json, const char *s, size_t len)
{
	if (json->error)
		return;

	flashmap_json_grow(json, len);
	if (json->error)
		return;

	memcpy(json->buf + json->len, s, len);
	json->len += len;
	json->buf[json->len] = '\0';
}

static void flashmap_json_appendf(struct flashmap_json_buf *json, const char *fmt, ...)
{
	va_list ap;
	va_list aq;
	int needed;

	if (json->error)
		return;

	va_start(ap, fmt);
	va_copy(aq, ap);
	needed = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);
	if (needed < 0) {
		va_end(ap);
		json->error = true;
		return;
	}

	flashmap_json_grow(json, needed);
	if (json->error) {
		va_end(ap);
		return;
	}

	vsnprintf(json->buf + json->len, json->cap - json->len, fmt, ap);
	json->len += needed;
	va_end(ap);
}

static void flashmap_json_append_escaped_string(struct flashmap_json_buf *json, const char *s)
{
	unsigned char ch;
	char esc[7];

	flashmap_json_append_raw(json, "\"", 1);

	for (; !json->error && *s; s++) {
		ch = (unsigned char)*s;
		switch (ch) {
		case '"':
			flashmap_json_append_raw(json, "\\\"", 2);
			break;
		case '\\':
			flashmap_json_append_raw(json, "\\\\", 2);
			break;
		case '\b':
			flashmap_json_append_raw(json, "\\b", 2);
			break;
		case '\f':
			flashmap_json_append_raw(json, "\\f", 2);
			break;
		case '\n':
			flashmap_json_append_raw(json, "\\n", 2);
			break;
		case '\r':
			flashmap_json_append_raw(json, "\\r", 2);
			break;
		case '\t':
			flashmap_json_append_raw(json, "\\t", 2);
			break;
		default:
			if (ch < 0x20) {
				snprintf(esc, sizeof(esc), "\\u%04x", ch);
				flashmap_json_append_raw(json, esc, 6);
			} else {
				flashmap_json_append_raw(json, (const char *)&ch, 1);
			}
			break;
		}
	}

	flashmap_json_append_raw(json, "\"", 1);
}

static int zipper_add_programmer_images(zip_t *zip, struct list_head *name_entries,
					char **programmer_entries,
					struct sahara_image *images)
{
	char *basename_name;
	char *zip_name;
	int count = 0;
	int i;
	int ret;

	for (i = 0; i < MAPPING_SZ; i++) {
		if (!images[i].ptr)
			continue;

		basename_name = zipper_basename(images[i].name ? images[i].name : "programmer.bin");
		if (!basename_name)
			return -1;

		if (zipper_name_is_used(name_entries, basename_name)) {
			zip_name = calloc(1, strlen(basename_name) + 20);
			if (!zip_name) {
				free(basename_name);
				return -1;
			}
			snprintf(zip_name, strlen(basename_name) + 20, "sahara%u_%s", i, basename_name);
			free(basename_name);
		} else {
			zip_name = basename_name;
		}

		if (zipper_name_is_used(name_entries, zip_name)) {
			ux_err("duplicate zip filename \"%s\"\n", zip_name);
			free(zip_name);
			return -1;
		}

		ret = zipper_add_buffer(zip, zip_name, images[i].ptr, images[i].len);
		if (ret) {
			free(zip_name);
			return ret;
		}

		ret = zipper_add_name_entry(name_entries, zip_name);
		if (ret) {
			free(zip_name);
			return ret;
		}

		programmer_entries[i] = zip_name;
		count++;
	}

	return count ? 0 : -1;
}

static void zipper_free_programmer_entries(char **programmer_entries)
{
	int i;

	for (i = 0; i < MAPPING_SZ; i++) {
		free(programmer_entries[i]);
		programmer_entries[i] = NULL;
	}
}

static int zipper_add_storage_xml(zip_t *zip, struct list_head *name_entries,
				  struct zipper_storage_ctx *storage)
{
	xmlChar *xml_data;
	int len;
	int ret;

	storage->program_xml_name = calloc(1, strlen(storage->memory) + strlen("_rawprogram.xml") + 1);
	if (!storage->program_xml_name)
		return -1;
	snprintf(storage->program_xml_name, strlen(storage->memory) + strlen("_rawprogram.xml") + 1,
		 "%s_rawprogram.xml", storage->memory);

	storage->patch_xml_name = calloc(1, strlen(storage->memory) + strlen("_patch.xml") + 1);
	if (!storage->patch_xml_name)
		return -1;
	snprintf(storage->patch_xml_name, strlen(storage->memory) + strlen("_patch.xml") + 1,
		 "%s_patch.xml", storage->memory);

	if (zipper_name_is_used(name_entries, storage->program_xml_name) ||
	    zipper_name_is_used(name_entries, storage->patch_xml_name)) {
		ux_err("duplicate xml filename for memory \"%s\"\n", storage->memory);
		return -1;
	}

	xmlDocDumpMemory(storage->program_doc, &xml_data, &len);
	if (!xml_data || len < 0)
		return -1;
	ret = zipper_add_buffer(zip, storage->program_xml_name, xml_data, (size_t)len);
	xmlFree(xml_data);
	if (ret)
		return ret;
	ret = zipper_add_name_entry(name_entries, storage->program_xml_name);
	if (ret)
		return ret;

	xmlDocDumpMemory(storage->patch_doc, &xml_data, &len);
	if (!xml_data || len < 0)
		return -1;
	ret = zipper_add_buffer(zip, storage->patch_xml_name, xml_data, (size_t)len);
	xmlFree(xml_data);
	if (ret)
		return ret;
	ret = zipper_add_name_entry(name_entries, storage->patch_xml_name);
	if (ret)
		return ret;

	return 0;
}

static int flashmap_build_json(struct flashmap_json_buf *json,
			       char **programmer_entries,
			       struct list_head *storages)
{
	struct zipper_storage_ctx *storage;
	bool first;
	int i;

	flashmap_json_appendf(json, "{\n");
	flashmap_json_appendf(json, "  \"version\": \"1.2.0-qdl\",\n");
	flashmap_json_appendf(json, "  \"products\": [\n");
	flashmap_json_appendf(json, "    {\n");
	flashmap_json_appendf(json, "      \"name\": \"contents\",\n");
	flashmap_json_appendf(json, "      \"layouts\": [\n");
	flashmap_json_appendf(json, "        {\n");
	flashmap_json_appendf(json, "          \"name\": \"layout0\",\n");
	flashmap_json_appendf(json, "          \"programmer\": {\n");

	first = true;
	for (i = 0; i < MAPPING_SZ; i++) {
		if (!programmer_entries[i])
			continue;

		if (!first)
			flashmap_json_appendf(json, ",\n");
		first = false;

		flashmap_json_appendf(json, "            \"%u\": ", i);
		flashmap_json_append_escaped_string(json, programmer_entries[i]);
	}
	flashmap_json_appendf(json, "\n");
	flashmap_json_appendf(json, "          },\n");
	flashmap_json_appendf(json, "          \"programmable\": [\n");

	first = true;
	list_for_each_entry(storage, storages, node) {
		if (!first)
			flashmap_json_appendf(json, ",\n");
		first = false;

		flashmap_json_appendf(json, "            {\n");
		flashmap_json_appendf(json, "              \"memory\": \"%s\",\n", storage->memory);
		flashmap_json_appendf(json, "              \"slot\": 0,\n");
		flashmap_json_appendf(json, "              \"files\": [\n");
		flashmap_json_appendf(json, "                ");
		flashmap_json_append_escaped_string(json, storage->program_xml_name);
		flashmap_json_appendf(json, ",\n");
		flashmap_json_appendf(json, "                ");
		flashmap_json_append_escaped_string(json, storage->patch_xml_name);
		flashmap_json_appendf(json, "\n");
		flashmap_json_appendf(json, "              ]\n");
		flashmap_json_appendf(json, "            }");
	}

	flashmap_json_appendf(json, "\n");
	flashmap_json_appendf(json, "          ]\n");
	flashmap_json_appendf(json, "        }\n");
	flashmap_json_appendf(json, "      ]\n");
	flashmap_json_appendf(json, "    }\n");
	flashmap_json_appendf(json, "  ]\n");
	flashmap_json_appendf(json, "}\n");

	return json->error ? -1 : 0;
}

int zipper_write(const char *filename, struct list_head *ops, struct sahara_image *images)
{
	char *programmer_entries[MAPPING_SZ] = {0};
	struct list_head file_renames = LIST_INIT(file_renames);
	struct zipper_storage_ctx *storage = NULL;
	struct list_head storages = LIST_INIT(storages);
	struct list_head name_entries = LIST_INIT(name_entries);
	struct flashmap_json_buf json = {0};
	enum qdl_storage_type current_storage = QDL_STORAGE_UNKNOWN;
	unsigned int raw_idx = 0;
	unsigned int fill_idx = 0;
	struct firehose_op *op;
	unsigned int file_offset;
	const char *zip_name;
	char *sparse_zip_name;
	char *tmp_filename = NULL;
	zip_t *zip;
	int tmp_fd;
	unsigned int programmer_count = 0;
	unsigned int program_count = 0;
	unsigned int patch_count = 0;
	unsigned int erase_count = 0;
	int zip_error = 0;
	int ret = -1;

	tmp_filename = malloc(strlen(filename) + 8);
	if (!tmp_filename)
		goto out_cleanup;
	sprintf(tmp_filename, "%s.XXXXXX", filename);

	tmp_fd = mkstemp(tmp_filename);
	if (tmp_fd < 0) {
		ux_err("failed to create temporary output zip \"%s\": %s\n",
		       tmp_filename, strerror(errno));
		goto out_cleanup;
	}
	close(tmp_fd);

	zip = zip_open(tmp_filename, ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
	if (!zip) {
		ux_err("failed to open output zip \"%s\"\n", tmp_filename);
		ret = -1;
		goto out_cleanup;
	}

	list_for_each_entry(op, ops, node) {
		if (op->type == FIREHOSE_OP_CONFIGURE) {
			current_storage = op->storage_type;
			storage = zipper_find_storage_ctx(&storages, current_storage);
			if (!storage) {
				storage = zipper_create_storage_ctx(current_storage);
				if (!storage)
					goto out_discard_zip;

				list_append(&storages, &storage->node);
			}
			continue;
		}

		if (!storage) {
			ux_err("internal error: missing configure operation\n");
			goto out_discard_zip;
		}

		switch (op->type) {
		case FIREHOSE_OP_PROGRAM:
			if (!op->filename)
				break;
			if (op->zip) {
				ux_err("create-zip does not support program entries sourced from zip archives\n");
				goto out_discard_zip;
			}

			sparse_zip_name = NULL;
			zip_name = NULL;

			if (op->sparse) {
				switch (op->sparse_chunk_type) {
				case CHUNK_TYPE_RAW:
					ret = zipper_register_raw_program_file(zip, &name_entries, storage->memory, op,
									       &raw_idx, &sparse_zip_name);
					break;
				case CHUNK_TYPE_FILL:
					ret = zipper_register_fill_program_file(zip, &name_entries, storage->memory, op,
										&fill_idx, &sparse_zip_name);
					break;
				default:
					ux_err("unsupported sparse chunk type %u in \"%s\"\n",
					       op->sparse_chunk_type, op->filename);
					goto out_discard_zip;
				}
				if (ret)
					goto out_discard_zip;

				zip_name = sparse_zip_name;
				file_offset = 0;
			} else {
				ret = zipper_register_program_file(zip, &name_entries, &file_renames,
								   current_storage, storage->memory,
								   op->filename, &zip_name);
				if (ret)
					goto out_discard_zip;

				file_offset = op->file_offset;
			}

			ret = zipper_storage_append_program(storage, op, zip_name, file_offset);
			free(sparse_zip_name);
			if (ret)
				goto out_discard_zip;
			program_count++;
			break;
		case FIREHOSE_OP_ERASE:
			ret = zipper_storage_append_erase(storage, op);
			if (ret)
				goto out_discard_zip;
			erase_count++;
			break;
		case FIREHOSE_OP_PATCH:
			/*
			 * Only filename=="DISK" entries are actually flashed,
			 * so omit the others from the zip file as well.
			 */
			if (!op->filename || strcmp(op->filename, "DISK"))
				break;

			ret = zipper_storage_append_patch(storage, op);
			if (ret)
				goto out_discard_zip;
			patch_count++;
			break;
		default:
			break;
		}
	}

	if (list_empty(&storages)) {
		ux_err("no configurable storage operations to write\n");
		goto out_discard_zip;
	}

	ret = zipper_add_programmer_images(zip, &name_entries, programmer_entries, images);
	if (ret) {
		ux_err("no programmer images available to write\n");
		goto out_discard_zip;
	}
	for (int i = 0; i < MAPPING_SZ; i++) {
		if (programmer_entries[i])
			programmer_count++;
	}

	list_for_each_entry(storage, &storages, node) {
		ret = zipper_add_storage_xml(zip, &name_entries, storage);
		if (ret)
			goto out_discard_zip;
	}

	ret = flashmap_build_json(&json, programmer_entries, &storages);
	if (ret)
		goto out_discard_zip;

	ret = zipper_add_buffer(zip, "flashmap.json", json.buf, json.len);
	if (ret)
		goto out_discard_zip;

	ux_info("found %u programmers, %u program entries, %u patch entries, %u erase entries\n",
		programmer_count, program_count, patch_count, erase_count);
	ux_info("finalizing archive, this can take a while for large images\n");
	if (zip_register_progress_callback_with_state(zip, 0.001,
						      zipper_close_progress_cb,
						      NULL, (void *)filename) < 0)
		ux_debug("unable to register zip close progress callback\n");

	if (zip_close(zip) < 0) {
		ux_err("failed to finalize output zip\n");
		zip_discard(zip);
		ret = -1;
		goto out_cleanup;
	}

	if (rename(tmp_filename, filename) < 0) {
		ux_err("failed to rename temporary output zip \"%s\" to \"%s\": %s\n",
		       tmp_filename, filename, strerror(errno));
		ret = -1;
		goto out_cleanup;
	}

	ux_info("successfully created %s\n", filename);

	ret = 0;
	goto out_cleanup;

out_discard_zip:
	ret = -1;
	zip_discard(zip);

out_cleanup:
	if (ret && tmp_filename)
		unlink(tmp_filename);
	free(tmp_filename);
	free(json.buf);
	zipper_free_name_entries(&name_entries);
	zipper_free_file_renames(&file_renames);
	zipper_free_programmer_entries(programmer_entries);
	zipper_free_storage_contexts(&storages);

	return ret;
}
