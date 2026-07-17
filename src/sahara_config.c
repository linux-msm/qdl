// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * All rights reserved.
 */
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "contents.h"
#include "pathbuf.h"
#include "qdl.h"
#include "oscompat.h"

/**
 * decode_sahara_config() - Attempt to decode a Sahara config XML document
 * @blob: Loaded image to be decoded as Sahara config
 * @images: List of Sahara images, with @images[0] populated
 *
 * The single blob provided in @images[0] might be a XML blob containing
 * a sahara_config document with definitions of the various Sahara images that
 * will be loaded. Attempt to parse this and if possible load each referenced
 * Sahara image into the @images array.
 *
 * The original blob (in @images[0]) is freed once it has been consumed.
 *
 * Lives in its own translation unit so that both the qdl binary and the
 * out-of-tree nbdkit plugin can link it, while the contents unit tests can
 * still stub it out.
 *
 * Returns: 0 if no archive was found, 1 if archive was decoded, -1 on error
 */
int decode_sahara_config(struct sahara_image *blob, struct sahara_image *images,
			 struct contents_filter *contents_filter)
{
	char image_path_full[PATH_MAX];
	struct pathbuf image_full_path = {};
	const char *image_path;
	unsigned int image_id;
	size_t image_path_len;
	xmlNode *images_node;
	xmlNode *image_node;
	char *blob_name_buf;
	size_t base_path_len;
	char *base_path;
	xmlNode *root;
	xmlDoc *doc;
	int errors = 0;
	int ret;

	if (blob->len < 5 || memcmp(blob->ptr, "<?xml", 5))
		return 0;

	doc = xmlReadMemory(blob->ptr, blob->len, blob->name, NULL, 0);
	if (!doc) {
		ux_err("failed to parse sahara_config in \"%s\"\n", blob->name);
		return -1;
	}

	blob_name_buf = strdup(blob->name);
	base_path = dirname(blob_name_buf);
	base_path_len = strlen(base_path);

	root = xmlDocGetRootElement(doc);
	if (xmlStrcmp(root->name, (xmlChar *)"sahara_config")) {
		ux_err("specified sahara_config \"%s\" is not a Sahara config\n", blob->name);
		goto err_free_doc;
	}

	for (images_node = root->children; images_node; images_node = images_node->next) {
		if (images_node->type == XML_ELEMENT_NODE &&
		    !xmlStrcmp(images_node->name, (xmlChar *)"images"))
			break;
	}

	if (!images_node) {
		ux_err("no images definitions found in sahara_config \"%s\"\n", blob->name);
		goto err_free_doc;
	}

	for (image_node = images_node->children; image_node; image_node = image_node->next) {
		if (image_node->type != XML_ELEMENT_NODE ||
		    xmlStrcmp(image_node->name, (xmlChar *)"image"))
			continue;

		image_id = attr_as_unsigned(image_node, "image_id", &errors);
		image_path = attr_as_string(image_node, "image_path", &errors);

		if (image_id == 0 || image_id >= MAPPING_SZ || errors || !image_path) {
			ux_err("invalid sahara_config image in \"%s\"\n", blob->name);
			free((void *)image_path);
			goto err_free_doc;
		}

		image_path_len = strlen(image_path);

		if (contents_resolve_path(contents_filter, image_path, &image_full_path) == 1) {
			memcpy(image_path_full, image_full_path.buf, image_full_path.len + 1);
		} else if (path_is_absolute(image_path)) {
			if (image_path_len + 1 > PATH_MAX) {
				free((void *)image_path);
				goto err_free_doc;
			}
			memcpy(image_path_full, image_path, image_path_len + 1);
		} else {
			if (base_path_len + 1 + image_path_len + 1 > PATH_MAX) {
				free((void *)image_path);
				goto err_free_doc;
			}
			memcpy(image_path_full, base_path, base_path_len);
			image_path_full[base_path_len] = '/';
			memcpy(image_path_full + base_path_len + 1, image_path, image_path_len);
			image_path_full[base_path_len + 1 + image_path_len] = '\0';
		}

		free((void *)image_path);

		ret = load_sahara_image(NULL, image_path_full, &images[image_id]);
		if (ret < 0)
			goto err_free_doc;
	}

	xmlFreeDoc(doc);
	free(blob_name_buf);

	free(blob->ptr);
	blob->ptr = NULL;
	blob->len = 0;

	return 1;

err_free_doc:
	sahara_images_free(images, MAPPING_SZ);
	free(blob->ptr);
	blob->ptr = NULL;
	blob->len = 0;
	xmlFreeDoc(doc);
	free(blob_name_buf);
	return -1;
}
