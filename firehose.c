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
#define _FILE_OFFSET_BITS 64
#include <sys/stat.h>
#include <sys/time.h>
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
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "sparse.h"
#include "qdl.h"
#include "ufs.h"

enum {
	FIREHOSE_ACK = 0,
	FIREHOSE_NAK,
};

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
		ux_err("failed to parse firehose response\n");
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
		ux_err("firehose response without data tag\n");
		*error = -EINVAL;
		xmlFreeDoc(doc);
		return NULL;
	}

	for (node = node->children; node && node->type != XML_ELEMENT_NODE; node = node->next)
		;

	if (!node) {
		ux_err("empty firehose response\n");
		*error = -EINVAL;
	}

	return node;
}

static int firehose_generic_parser(xmlNode *node, void *data)
{
	xmlChar *value;
	int ret = -EINVAL;

	value = xmlGetProp(node, (xmlChar*)"value");
	if (!value)
		return -EINVAL;

	if (xmlStrcmp(node->name, (xmlChar*)"log") == 0) {
		ux_log("LOG: %s\n", value);
		ret = -EAGAIN;
	} else if (xmlStrcmp(value, (xmlChar*)"ACK") == 0) {
		ret = FIREHOSE_ACK;
	} else if (xmlStrcmp(value, (xmlChar*)"NAK") == 0) {
		ret = FIREHOSE_NAK;
	}

	xmlFree(value);

	return ret;
}

static int firehose_read(struct qdl_device *qdl, int timeout_ms,
			 int (*response_parser)(xmlNode *node, void *data),
			 void *data)
{
	char buf[4096];
	xmlNode *node;
	int error;
	int ret = -EAGAIN;
	int n;
	struct timeval timeout;
	struct timeval now;
	struct timeval delta = { .tv_sec = timeout_ms / 1000,
				 .tv_usec = (timeout_ms % 1000) * 1000 };

	gettimeofday(&now, NULL);
	timeradd(&now, &delta, &timeout);

	do {
		n = qdl_read(qdl, buf, sizeof(buf), 100);
		if (n < 0) {
			gettimeofday(&now, NULL);
			if (timercmp(&now, &timeout, <))
				continue;

			return -ETIMEDOUT;
		}
		buf[n] = '\0';

		ux_debug("FIREHOSE READ: %s\n", buf);

		node = firehose_response_parse(buf, n, &error);
		if (!node)
			return error;

		ret = response_parser(node, data);
		xmlFreeDoc(node->doc);
	} while (ret == -EAGAIN);

	return ret;
}

static int firehose_write(struct qdl_device *qdl, xmlDoc *doc)
{
	int saved_errno;
	xmlChar *s;
	int len;
	int ret;

	xmlDocDumpMemory(doc, &s, &len);

	for (;;) {
		ux_debug("FIREHOSE WRITE: %s\n", s);

		ret = qdl_write(qdl, s, len);
		saved_errno = errno;

		/*
		 * db410c sometimes sense a <response> followed by <log>
		 * entries and won't accept write commands until these are
		 * drained, so attempt to read any pending data and then retry
		 * the write.
		 */
		if (ret < 0 && errno == ETIMEDOUT) {
			firehose_read(qdl, 100, firehose_generic_parser, NULL);
		} else {
			break;
		}
	}
	xmlFree(s);
	return ret < 0 ? -saved_errno : 0;
}

static size_t max_payload_size = 1048576;

/**
 * firehose_configure_response_parser() - parse a configure response
 * @node:	response xmlNode
 *
 * Return: max size supported by the remote, or negative errno on failure
 */
static int firehose_configure_response_parser(xmlNode *node, void *data)
{
	xmlChar *payload;
	xmlChar *value;
	size_t max_size;

	value = xmlGetProp(node, (xmlChar*)"value");
	if (!value)
		return -EINVAL;

	if (xmlStrcmp(node->name, (xmlChar*)"log") == 0) {
		ux_log("LOG: %s\n", value);
		xmlFree(value);
		return -EAGAIN;
	}

	payload = xmlGetProp(node, (xmlChar*)"MaxPayloadSizeToTargetInBytes");
	if (!payload) {
		xmlFree(value);
		return -EINVAL;
	}

	max_size = strtoul((char*)payload, NULL, 10);
	xmlFree(payload);

	/*
	 * When receiving an ACK the remote may indicate that we should attempt
	 * a larger payload size
	 */
	if (!xmlStrcmp(value, (xmlChar*)"ACK")) {
		payload = xmlGetProp(node, (xmlChar*)"MaxPayloadSizeToTargetInBytesSupported");
		if (!payload)
			return -EINVAL;

		max_size = strtoul((char*)payload, NULL, 10);
		xmlFree(payload);
	}

	*(size_t*)data = max_size;
	xmlFree(value);

	return FIREHOSE_ACK;
}

static int firehose_send_configure(struct qdl_device *qdl, size_t payload_size, bool skip_storage_init, const char *storage, size_t *max_payload_size)
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
	xml_setpropf(node, "MaxPayloadSizeToTargetInBytes", "%lu", payload_size);
	xml_setpropf(node, "verbose", "%d", 0);
	xml_setpropf(node, "ZLPAwareHost", "%d", 1);
	xml_setpropf(node, "SkipStorageInit", "%d", skip_storage_init);

	ret = firehose_write(qdl, doc);
	xmlFreeDoc(doc);
	if (ret < 0)
		return ret;

	return firehose_read(qdl, 5000, firehose_configure_response_parser, max_payload_size);
}

static int firehose_configure(struct qdl_device *qdl, bool skip_storage_init, const char *storage)
{
	size_t size = 0;
	int ret;

	ret = firehose_send_configure(qdl, max_payload_size, skip_storage_init, storage, &size);
	if (ret < 0) {
		ux_err("configure request failed\n");
		return -1;
	}

	/* Retry if remote proposed different size */
	if (size != max_payload_size) {
		ret = firehose_send_configure(qdl, size, skip_storage_init, storage, &size);
		if (ret != FIREHOSE_ACK) {
			ux_err("configure request with updated payload size failed\n");
			return -1;
		}

		max_payload_size = size;
	}

	ux_debug("accepted max payload size: %zu\n", max_payload_size);

	return 0;
}

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define ROUND_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

static int firehose_erase(struct qdl_device *qdl, struct program *program)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"erase", NULL);
	xml_setpropf(node, "PAGES_PER_BLOCK", "%d", program->pages_per_block);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", program->sector_size);
	xml_setpropf(node, "num_partition_sectors", "%d", program->num_sectors);
	xml_setpropf(node, "start_sector", "%s", program->start_sector);

	ret = firehose_write(qdl, doc);
	if (ret < 0) {
		ux_err("failed to send program request\n");
		goto out;
	}

	ret = firehose_read(qdl, 30000, firehose_generic_parser, NULL);
	if (ret)
		ux_err("failed to erase %s+0x%x\n", program->start_sector, program->num_sectors);
	else
		ux_info("successfully erased %s+0x%x\n", program->start_sector, program->num_sectors);

out:
	xmlFreeDoc(doc);
	return ret == FIREHOSE_ACK ? 0 : -1;
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
	uint32_t fill_value;

	num_sectors = program->num_sectors;

	ret = fstat(fd, &sb);
	if (ret < 0)
		err(1, "failed to stat \"%s\"\n", program->filename);

	if (!program->sparse) {
		num_sectors = (sb.st_size + program->sector_size - 1) / program->sector_size;

		if (program->num_sectors && num_sectors > program->num_sectors) {
			ux_err("%s to big for %s truncated to %d\n",
				program->filename,
				program->label,
				program->num_sectors * program->sector_size);
			num_sectors = program->num_sectors;
		}
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

	if (program->is_nand) {
		xml_setpropf(node, "PAGES_PER_BLOCK", "%d", program->pages_per_block);
		xml_setpropf(node, "last_sector", "%d", program->last_sector);
	}

	ret = firehose_write(qdl, doc);
	if (ret < 0) {
		ux_err("failed to send program request\n");
		goto out;
	}

	ret = firehose_read(qdl, 10000, firehose_generic_parser, NULL);
	if (ret) {
		ux_err("failed to setup programming\n");
		goto out;
	}

	t0 = time(NULL);

	if (!program->sparse)
		lseek(fd, (off_t) program->file_offset * program->sector_size, SEEK_SET);
	else {
		switch (program->sparse_chunk_type) {
			case CHUNK_TYPE_RAW:
				lseek(fd, (off_t) program->sparse_chunk_data, SEEK_SET);
				break;
			case CHUNK_TYPE_FILL:
				fill_value = (uint32_t) program->sparse_chunk_data;
				for (n = 0; n < max_payload_size; n += sizeof(fill_value))
					memcpy(buf + n, &fill_value, sizeof(fill_value));
				break;
			default:
				ux_err("[SPARSE] invalid chunk type\n");
				goto out;
		}
	}

	left = num_sectors;
	while (left > 0) {
		chunk_size = MIN(max_payload_size / program->sector_size, left);

		if (!program->sparse || program->sparse_chunk_type != CHUNK_TYPE_FILL) {
			n = read(fd, buf, chunk_size * program->sector_size);
			if (n < 0) {
				ux_err("failed to read %s\n", program->filename);
				goto out;
			}

			if (n < max_payload_size)
				memset(buf + n, 0, max_payload_size - n);
		}

		n = qdl_write(qdl, buf, chunk_size * program->sector_size);
		if (n < 0) {
			ux_err("USB write failed for data chunk\n");
			goto out;
		}

		if (n != chunk_size * program->sector_size) {
			ux_err("USB write truncated\n");
			ret = -1;
			goto out;
		}

		left -= chunk_size;

		ux_progress("%s", num_sectors - left, num_sectors, program->label);
	}

	t = time(NULL) - t0;

	ret = firehose_read(qdl, 30000, firehose_generic_parser, NULL);
	if (ret) {
		ux_err("flashing of %s failed\n", program->label);
	} else if (t) {
		ux_err("flashed \"%s\" successfully at %lukB/s\n",
			program->label,
			(unsigned long)program->sector_size * num_sectors / t / 1024);
	} else {
		ux_err("flashed \"%s\" successfully\n",
			program->label);
	}

out:
	xmlFreeDoc(doc);
	free(buf);

	return ret == FIREHOSE_ACK ? 0 : -1;
}

static int firehose_read_op(struct qdl_device *qdl, struct read_op *read_op, int fd)
{
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
	bool expect_empty;

	buf = malloc(max_payload_size);
	if (!buf)
		err(1, "failed to allocate sector buffer");

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"read", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", read_op->sector_size);
	xml_setpropf(node, "num_partition_sectors", "%d", read_op->num_sectors);
	xml_setpropf(node, "physical_partition_number", "%d", read_op->partition);
	xml_setpropf(node, "start_sector", "%s", read_op->start_sector);
	if (read_op->filename)
		xml_setpropf(node, "filename", "%s", read_op->filename);

	ret = firehose_write(qdl, doc);
	if (ret < 0) {
		ux_err("failed to send read command\n");
		goto out;
	}

	ret = firehose_read(qdl, 10000, firehose_generic_parser, NULL);
	if (ret) {
		ux_err("failed to setup reading operation\n");
		goto out;
	}

	t0 = time(NULL);

	left = read_op->num_sectors;
	expect_empty = false;
	while (left > 0 || expect_empty) {
		chunk_size = MIN(max_payload_size / read_op->sector_size, left);

		n = qdl_read(qdl, buf, chunk_size * read_op->sector_size, 30000);
		if (n < 0) {
			err(1, "failed to read");
		}

		if (n == 0 && expect_empty) {
			// Every second transfer is empty during this stage.
			expect_empty = false;
			continue;
		} else if (expect_empty) {
			err(1, "expected empty transfer but received non-empty transfer during read");
		} else if (n != chunk_size * read_op->sector_size){
			err(1, "failed to read full sector");
		}

		n = write(fd, buf, n);

		if (n != chunk_size * read_op->sector_size) {
			err(1, "failed to write");
		}

		left -= chunk_size;
		expect_empty = true;
	}

	ret = firehose_read(qdl, 10000, firehose_generic_parser, NULL);
	if (ret) {
		ux_err("read operation failed\n");
		goto out;
	}

	t = time(NULL) - t0;

	if (t) {
		ux_err("read \"%s\" successfully at %ldkB/s\n",
			read_op->filename,
			(unsigned long)read_op->sector_size * read_op->num_sectors / t / 1024);
	} else {
		ux_err("read \"%s\" successfully\n",
			read_op->filename);
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

	ux_debug("applying patch \"%s\"\n", patch->what);

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

	ret = firehose_read(qdl, 5000, firehose_generic_parser, NULL);
	if (ret)
		ux_err("patch application failed\n");

out:
	xmlFreeDoc(doc);
	return ret == FIREHOSE_ACK ? 0 : -1;
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

        ret = firehose_read(qdl, 5000, firehose_generic_parser, NULL);
        if (ret) {
                ux_err("ufs request failed\n");
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
	xml_setpropf(node_to_send, "bConfigDescrLock", "%d", ufs->bConfigDescrLock);

	if (ufs->wb) {
		xml_setpropf(node_to_send, "bWriteBoosterBufferPreserveUserSpaceEn", "%d", ufs->bWriteBoosterBufferPreserveUserSpaceEn);
		xml_setpropf(node_to_send, "bWriteBoosterBufferType", "%d", ufs->bWriteBoosterBufferType);
		xml_setpropf(node_to_send, "shared_wb_buffer_size_in_kb", "%d", ufs->shared_wb_buffer_size_in_kb);
	}

	ret = firehose_send_single_tag(qdl, node_to_send);
	if (ret)
		ux_err("failed to send ufs common tag\n");

	return ret == FIREHOSE_ACK ? 0 : -1;
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
		ux_err("failed to apply ufs body tag\n");

	return ret == FIREHOSE_ACK ? 0 : -1;
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
		ux_err("failed to apply ufs epilogue\n");

	return ret == FIREHOSE_ACK ? 0 : -1;
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
		return -1;

	ret = firehose_read(qdl, 5000, firehose_generic_parser, NULL);
	if (ret) {
		ux_err("failed to mark partition %d as bootable\n", part);
		return -1;
	}

	ux_info("partition %d is now bootable\n", part);
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
		return -1;

	ret = firehose_read(qdl, 5000, firehose_generic_parser, NULL);
	if (ret < 0)
		ux_err("failed to request device reset\n");
	/* drain any remaining log messages for reset */
	else
		firehose_read(qdl, 1000, firehose_generic_parser, NULL);

	return ret == FIREHOSE_ACK ? 0 : -1;
}

int firehose_run(struct qdl_device *qdl, const char *incdir, const char *storage, bool allow_missing)
{
	bool multiple;
	int bootable;
	int ret;

	ux_info("waiting for programmer...\n");

	firehose_read(qdl, 5000, firehose_generic_parser, NULL);

	if(ufs_need_provisioning()) {
		ret = firehose_configure(qdl, true, storage);
		if (ret)
			return ret;
		ret = ufs_provisioning_execute(qdl, firehose_apply_ufs_common,
			firehose_apply_ufs_body, firehose_apply_ufs_epilogue);
		if (!ret)
			ux_info("UFS provisioning succeeded\n");
		else
			ux_info("UFS provisioning failed\n");

		firehose_reset(qdl);

		return ret;
	}

	ret = firehose_configure(qdl, false, storage);
	if (ret)
		return ret;

	ret = erase_execute(qdl, firehose_erase);
	if (ret)
		return ret;

	ret = program_execute(qdl, firehose_program, incdir, allow_missing);
	if (ret)
		return ret;

	ret = patch_execute(qdl, firehose_apply_patch);
	if (ret)
		return ret;

	ret = read_op_execute(qdl, firehose_read_op, incdir);
	if (ret)
		return ret;

	bootable = program_find_bootable_partition(&multiple);
	if (bootable < 0) {
		ux_debug("no boot partition found\n");
	} else {
		if (multiple) {
			ux_info("Multiple candidates for primary bootloader found, using partition %d\n",
				bootable);
		}
		firehose_set_bootable(qdl, bootable);
	}

	firehose_reset(qdl);

	return 0;
}
