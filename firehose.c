/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "qdl.h"
#include "ufs.h"
#include "sparse.h"
#include "util.h"

static void xml_setpropf(xmlNode *node, const char *attr, const char *fmt, ...)
{
	xmlChar buf[128];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf((char*)buf, sizeof(buf), fmt, ap);
	xmlSetProp(node, (xmlChar*)attr, buf);
	va_end(ap);
}

static xmlNode *firehose_response_parse(const void *buf, size_t len, int *error)
{
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;

	doc = xmlReadMemory(buf, len, NULL, NULL, 0);
	if (!doc) {
		fprintf(stderr, "failed to parse firehose packet\n");
		*error = -EINVAL;
		return NULL;
	}

	root = xmlDocGetRootElement(doc);
	for (node = root; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrcmp(node->name, (xmlChar*)"data") == 0)
			break;
	}

	if (!node) {
		fprintf(stderr, "firehose packet without data tag\n");
		*error = -EINVAL;
		xmlFreeDoc(doc);
		return NULL;
	}

	for (node = node->children; node && node->type != XML_ELEMENT_NODE; node = node->next)
		;

	return node;
}

static void firehose_response_log(xmlNode *node)
{
	xmlChar *value;

	value = xmlGetProp(node, (xmlChar*)"value");
	printf("LOG: %s\n", value);
	xmlFree(value);
}

static int firehose_read(struct qdl_device *qdl, int wait, int (*response_parser)(xmlNode *node))
{
	char buf[4096];
	xmlNode *nodes;
	xmlNode *node;
	int error;
	char *msg;
	char *end;
	bool done = false;
	int ret = -ENXIO;
	int n;
	int timeout = 1000;

	if (wait > 0)
		timeout = wait;

	for (;;) {
		n = qdl_read(qdl, buf, sizeof(buf), timeout);
		if (n < 0) {
			if (done)
				break;

			warn("failed to read");
			return -ETIMEDOUT;
		}
		buf[n] = '\0';

		if (qdl_debug)
			fprintf(stderr, "FIREHOSE READ: %s\n", buf);

		for (msg = buf; msg[0]; msg = end) {
			end = strstr(msg, "</data>");
			if (!end) {
				fprintf(stderr, "firehose response truncated\n");
				exit(1);
			}

			end += strlen("</data>");

			nodes = firehose_response_parse(msg, end - msg, &error);
			if (!nodes) {
				fprintf(stderr, "unable to parse response\n");
				return error;
			}

			for (node = nodes; node; node = node->next) {
				if (xmlStrcmp(node->name, (xmlChar*)"log") == 0) {
					firehose_response_log(node);
				} else if (xmlStrcmp(node->name, (xmlChar*)"response") == 0) {
					if (!response_parser)
						fprintf(stderr, "received response with no parser\n");
					else
						ret = response_parser(node);
					done = true;
					timeout = 1;
				}
			}

			xmlFreeDoc(nodes->doc);
		}

		if (wait > 0)
			timeout = 100;
	}

	return ret;
}

static int firehose_write(struct qdl_device *qdl, xmlDoc *doc)
{
	int saved_errno;
	xmlChar *s;
	int len;
	int ret;

	xmlDocDumpMemory(doc, &s, &len);

	if (qdl_debug)
		fprintf(stderr, "FIREHOSE WRITE: %s\n", s);

	ret = qdl_write(qdl, s, len, true);
	saved_errno = errno;
	xmlFree(s);
	return ret < 0 ? -saved_errno : 0;
}

static int firehose_nop_parser(xmlNode *node)
{
	xmlChar *value;
	int ret;

	value = xmlGetProp(node, (xmlChar*)"value");
	ret = !!xmlStrcmp(value, (xmlChar*)"ACK");
	xmlFree(value);
	return ret;
}

static size_t max_payload_size = 1048576;

/**
 * firehose_configure_response_parser() - parse a configure response
 * @node:	response xmlNode
 *
 * Return: max size supported by the remote, or negative errno on failure
 */
static int firehose_configure_response_parser(xmlNode *node)
{
	xmlChar *payload;
	xmlChar *value;
	size_t max_size;

	value = xmlGetProp(node, (xmlChar*)"value");
	if (!value)
		return -EINVAL;

	payload = xmlGetProp(node, (xmlChar*)"MaxPayloadSizeToTargetInBytes");
	if (!payload) {
		xmlFree(value);
		return -EINVAL;
	}

	max_size = strtoul((char*)payload, NULL, 10);

	/*
	 * When receiving an ACK the remote may indicate that we should attempt
	 * a larger payload size
	 */
	if (!xmlStrcmp(value, (xmlChar*)"ACK")) {
		xmlFree(payload);
		payload = xmlGetProp(node, (xmlChar*)"MaxPayloadSizeToTargetInBytesSupported");
		if (!payload)
			return -EINVAL;

		max_size = strtoul((char*)payload, NULL, 10);
	}

	xmlFree(payload);
	xmlFree(value);

	return max_size;
}

static int firehose_send_configure(struct qdl_device *qdl, size_t payload_size, bool skip_storage_init, const char *storage)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"configure", NULL);
	xml_setpropf(node, "MemoryName", storage);
	xml_setpropf(node, "MaxPayloadSizeToTargetInBytes", "%d", payload_size);
	xml_setpropf(node, "verbose", "%d", 0);
	xml_setpropf(node, "ZLPAwareHost", "%d", 1);
	xml_setpropf(node, "SkipStorageInit", "%d", skip_storage_init);

	ret = firehose_write(qdl, doc);
	xmlFreeDoc(doc);
	if (ret < 0)
		return ret;

	return firehose_read(qdl, -1, firehose_configure_response_parser);
}

static int firehose_configure(struct qdl_device *qdl, bool skip_storage_init, const char *storage)
{
	int ret;

	ret = firehose_send_configure(qdl, max_payload_size, skip_storage_init, storage);
	if (ret < 0)
		return ret;

	/* Retry if remote proposed different size */
	if (ret != max_payload_size) {
		ret = firehose_send_configure(qdl, ret, skip_storage_init, storage);
		if (ret < 0)
			return ret;

		max_payload_size = ret;
	}

	if (qdl_debug) {
		fprintf(stderr, "[CONFIGURE] max payload size: %zu\n",
			max_payload_size);
	}

	return 0;
}

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define ROUND_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

struct sparse_cb_info_t {
	struct qdl_device *qdl;
	struct program *program;
	void *payload;
	size_t payload_len;
	size_t write_count;
	size_t total_write_count;
};

static int firehose_program_sparse(void *priv, const void *data, size_t len)
{
	int left;
	struct sparse_cb_info_t *priv_info = (struct sparse_cb_info_t *)priv;
	size_t chunk_size;
	size_t data_write_count = 0;
	int n;
	char *p_data = (char *)data;
	int ret = 0;

	left = (priv_info->payload_len + len) / priv_info->program->sector_size;
	while (left > 0) {
		chunk_size = MIN(max_payload_size / priv_info->program->sector_size, left);

		n = chunk_size * priv_info->program->sector_size - priv_info->payload_len;
		memcpy(priv_info->payload + priv_info->payload_len, p_data + data_write_count, n);
		data_write_count += n;

		priv_info->payload_len += n;
		if (priv_info->payload_len < max_payload_size && (priv_info->write_count + priv_info->payload_len) < priv_info->total_write_count) {
			if (chunk_size < left)
				continue;
			else
				break;
		}

		if (priv_info->payload_len < max_payload_size)
			memset(priv_info->payload + n, 0, max_payload_size - n);

		n = qdl_write(priv_info->qdl, priv_info->payload, chunk_size * priv_info->program->sector_size, true);
		if (n < 0) {
			err(1, "failed to write");
			ret = -1;
		}

		if (n != chunk_size * priv_info->program->sector_size) {
			err(1, "failed to write full sector");
			ret = -1;
		}

		priv_info->write_count += n;

		priv_info->payload_len = 0U;

		left -= chunk_size;
	}

	return ret;
}

static int firehose_program(struct qdl_device *qdl, struct program *program, int fd)
{
	unsigned num_sectors;
	struct stat sb;
	size_t chunk_size;
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	void *buf;
	time_t t0;
	time_t t;
	int left;
	int ret;
	int n;
	struct sparse_file *s_file = NULL;
	struct sparse_cb_info_t sparse_cb_info;

	num_sectors = program->num_sectors;

	ret = fstat(fd, &sb);
	if (ret < 0)
		err(1, "failed to stat \"%s\"\n", program->filename);

	lseek(fd, program->file_offset * program->sector_size, SEEK_SET);

	if (program->is_sparse) {
		s_file = sparse_file_import(fd, true);

		sparse_cb_info.total_write_count = sparse_file_len(s_file);

		num_sectors = (sparse_cb_info.total_write_count + program->sector_size - 1) / program->sector_size;
	} else {
		num_sectors = (sb.st_size + program->sector_size - 1) / program->sector_size;
	}

	if (program->num_sectors && num_sectors > program->num_sectors) {
		fprintf(stderr, "[PROGRAM] %s truncated to %d\n",
			program->label,
			program->num_sectors * program->sector_size);
		num_sectors = program->num_sectors;
	}

	buf = malloc(max_payload_size);
	if (!buf)
		err(1, "failed to allocate sector buffer");

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"program", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", program->sector_size);
	xml_setpropf(node, "num_partition_sectors", "%d", num_sectors);
	xml_setpropf(node, "physical_partition_number", "%d", program->partition);
	xml_setpropf(node, "start_sector", "%s", program->start_sector);
	if (program->filename)
		xml_setpropf(node, "filename", "%s", program->filename);

	ret = firehose_write(qdl, doc);
	if (ret < 0) {
		fprintf(stderr, "[PROGRAM] failed to write program command\n");
		goto out;
	}

	ret = firehose_read(qdl, -1, firehose_nop_parser);
	if (ret) {
		fprintf(stderr, "[PROGRAM] failed to setup programming\n");
		goto out;
	}

	t0 = time(NULL);

	if (program->is_sparse) {
		sparse_cb_info.payload = buf;
		sparse_cb_info.payload_len = 0U;
		sparse_cb_info.write_count = 0U;
		sparse_cb_info.program = program;
		sparse_cb_info.qdl = qdl;

		ret = sparse_stream_callback(s_file, firehose_program_sparse, &sparse_cb_info);

		sparse_file_destroy(s_file);
	} else {
		left = num_sectors;
		while (left > 0) {
			chunk_size = MIN(max_payload_size / program->sector_size, left);

			n = read(fd, buf, chunk_size * program->sector_size);
			if (n < 0)
				err(1, "failed to read");

			if (n < max_payload_size)
				memset(buf + n, 0, max_payload_size - n);
			
			n = qdl_write(qdl, buf, chunk_size * program->sector_size, true);
			if (n < 0)
				err(1, "failed to write");

			if (n != chunk_size * program->sector_size)
				err(1, "failed to write full sector");

			left -= chunk_size;
		}
	}

	t = time(NULL) - t0;

	ret = firehose_read(qdl, -1, firehose_nop_parser);
	if (ret) {
		fprintf(stderr, "[PROGRAM] failed\n");
	} else if (t) {
		fprintf(stderr,
			"[PROGRAM] flashed \"%s\" successfully at %ldkB/s\n",
			program->label,
			program->sector_size * num_sectors / t / 1024);
	} else {
		fprintf(stderr, "[PROGRAM] flashed \"%s\" successfully\n",
			program->label);
	}

out:
	xmlFreeDoc(doc);
	free(buf);
	return ret;
}

static int firehose_apply_patch(struct qdl_device *qdl, struct patch *patch)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	printf("%s\n", patch->what);

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"patch", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", patch->sector_size);
	xml_setpropf(node, "byte_offset", "%d", patch->byte_offset);
	xml_setpropf(node, "filename", "%s", patch->filename);
	xml_setpropf(node, "physical_partition_number", "%d", patch->partition);
	xml_setpropf(node, "size_in_bytes", "%d", patch->size_in_bytes);
	xml_setpropf(node, "start_sector", "%s", patch->start_sector);
	xml_setpropf(node, "value", "%s", patch->value);

	ret = firehose_write(qdl, doc);
	if (ret < 0)
		goto out;

	ret = firehose_read(qdl, -1, firehose_nop_parser);
	if (ret)
		fprintf(stderr, "[APPLY PATCH] %d\n", ret);

out:
	xmlFreeDoc(doc);
	return ret;
}

static int firehose_send_single_tag(struct qdl_device *qdl, xmlNode *node){
        xmlNode *root;
        xmlDoc *doc;
        int ret;

        doc = xmlNewDoc((xmlChar*)"1.0");
        root = xmlNewNode(NULL, (xmlChar*)"data");
        xmlDocSetRootElement(doc, root);
        xmlAddChild(root, node);

        ret = firehose_write(qdl, doc);
        if (ret < 0)
                goto out;

        ret = firehose_read(qdl, -1, firehose_nop_parser);
        if (ret) {
                fprintf(stderr, "[UFS] %s err %d\n", __func__, ret);
                ret = -EINVAL;
        }

out:
        xmlFreeDoc(doc);
        return ret;
}

int firehose_apply_ufs_common(struct qdl_device *qdl, struct ufs_common *ufs)
{
	xmlNode *node_to_send;
	int ret;

	node_to_send = xmlNewNode (NULL, (xmlChar*)"ufs");

	xml_setpropf(node_to_send, "bNumberLU", "%d", ufs->bNumberLU);
	xml_setpropf(node_to_send, "bBootEnable", "%d", ufs->bBootEnable);
	xml_setpropf(node_to_send, "bDescrAccessEn", "%d", ufs->bDescrAccessEn);
	xml_setpropf(node_to_send, "bInitPowerMode", "%d", ufs->bInitPowerMode);
	xml_setpropf(node_to_send, "bHighPriorityLUN", "%d", ufs->bHighPriorityLUN);
	xml_setpropf(node_to_send, "bSecureRemovalType", "%d", ufs->bSecureRemovalType);
	xml_setpropf(node_to_send, "bInitActiveICCLevel", "%d", ufs->bInitActiveICCLevel);
	xml_setpropf(node_to_send, "wPeriodicRTCUpdate", "%d", ufs->wPeriodicRTCUpdate);
	xml_setpropf(node_to_send, "bConfigDescrLock", "%d", 0/*ufs->bConfigDescrLock*/); //Safety, remove before fly

	ret = firehose_send_single_tag(qdl, node_to_send);
	if (ret)
		fprintf(stderr, "[APPLY UFS common] %d\n", ret);

	return ret;
}

int firehose_apply_ufs_body(struct qdl_device *qdl, struct ufs_body *ufs)
{
	xmlNode *node_to_send;
	int ret;

	node_to_send = xmlNewNode (NULL, (xmlChar*)"ufs");

	xml_setpropf(node_to_send, "LUNum", "%d", ufs->LUNum);
	xml_setpropf(node_to_send, "bLUEnable", "%d", ufs->bLUEnable);
	xml_setpropf(node_to_send, "bBootLunID", "%d", ufs->bBootLunID);
	xml_setpropf(node_to_send, "size_in_kb", "%d", ufs->size_in_kb);
	xml_setpropf(node_to_send, "bDataReliability", "%d", ufs->bDataReliability);
	xml_setpropf(node_to_send, "bLUWriteProtect", "%d", ufs->bLUWriteProtect);
	xml_setpropf(node_to_send, "bMemoryType", "%d", ufs->bMemoryType);
	xml_setpropf(node_to_send, "bLogicalBlockSize", "%d", ufs->bLogicalBlockSize);
	xml_setpropf(node_to_send, "bProvisioningType", "%d", ufs->bProvisioningType);
	xml_setpropf(node_to_send, "wContextCapabilities", "%d", ufs->wContextCapabilities);
	if(ufs->desc)
		xml_setpropf(node_to_send, "desc", "%s", ufs->desc);

	ret = firehose_send_single_tag(qdl, node_to_send);
	if (ret)
		fprintf(stderr, "[APPLY UFS body] %d\n", ret);

	return ret;
}

int firehose_apply_ufs_epilogue(struct qdl_device *qdl, struct ufs_epilogue *ufs,
	bool commit)
{
	xmlNode *node_to_send;
	int ret;

	node_to_send = xmlNewNode (NULL, (xmlChar*)"ufs");

	xml_setpropf(node_to_send, "LUNtoGrow", "%d", ufs->LUNtoGrow);
	xml_setpropf(node_to_send, "commit", "%d", commit);

	ret = firehose_send_single_tag(qdl, node_to_send);
	if (ret)
		fprintf(stderr, "[APPLY UFS epilogue] %d\n", ret);

	return ret;
}

static int firehose_set_bootable(struct qdl_device *qdl, int part)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"setbootablestoragedrive", NULL);
	xml_setpropf(node, "value", "%d", part);

	ret = firehose_write(qdl, doc);
	xmlFreeDoc(doc);
	if (ret < 0)
		return ret;

	ret = firehose_read(qdl, -1, firehose_nop_parser);
	if (ret) {
		fprintf(stderr, "failed to mark partition %d as bootable\n", part);
		return -1;
	}

	printf("partition %d is now bootable\n", part);
	return 0;
}

static int firehose_reset(struct qdl_device *qdl)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"power", NULL);
	xml_setpropf(node, "value", "reset");

	ret = firehose_write(qdl, doc);
	xmlFreeDoc(doc);
	if (ret < 0)
		return ret;

	return firehose_read(qdl, -1, firehose_nop_parser);
}

int firehose_run(struct qdl_device *qdl, const char *incdir, const char *storage)
{
	int bootable;
	int ret;

	/* Wait for the firehose payload to boot */
	sleep(3);

	firehose_read(qdl, 1000, NULL);

	if(ufs_need_provisioning()) {
		ret = firehose_configure(qdl, true, storage);
		if (ret)
			return ret;
		ret = ufs_provisioning_execute(qdl, firehose_apply_ufs_common,
			firehose_apply_ufs_body, firehose_apply_ufs_epilogue);
		if (!ret)
			printf("UFS provisioning succeeded\n");
		else
			printf("UFS provisioning failed\n");
		return ret;
	}

	ret = firehose_configure(qdl, false, storage);
	if (ret)
		return ret;

	ret = program_execute(qdl, firehose_program, incdir);
	if (ret)
		return ret;

	ret = patch_execute(qdl, firehose_apply_patch);
	if (ret)
		return ret;

	bootable = program_find_bootable_partition();
	if (bootable < 0)
		fprintf(stderr, "no boot partition found\n");
	else
		firehose_set_bootable(qdl, bootable);

	firehose_reset(qdl);

	return 0;
}
