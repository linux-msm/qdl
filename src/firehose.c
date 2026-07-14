// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * All rights reserved.
 */
#include "list.h"
#define _FILE_OFFSET_BITS 64
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "qdl.h"
#include "file.h"
#include "firehose.h"
#include "sha2.h"
#include "ufs.h"
#include "oscompat.h"
#include "vip.h"
#include "sparse.h"
#include "gpt.h"

enum {
	FIREHOSE_ACK = 0,
	FIREHOSE_NAK,
};

/*
 * Substring emitted by the Firehose programmer's startup logs when VIP is
 * active on-device (e.g. "INFO: VIP is enabled, receiving the signed table
 * of size 8192"). Matching a stable prefix avoids coupling to the trailing
 * byte count, which varies per programmer build.
 */
#define VIP_PROGRAMMER_MARKER "VIP is enabled, receiving the signed table"

static void firehose_check_vip_marker(struct qdl_device *qdl, xmlNode *node)
{
	xmlChar *value;

	if (qdl->vip_data.programmer_requires_vip)
		return;
	if (xmlStrcmp(node->name, (xmlChar *)"log") != 0)
		return;

	value = xmlGetProp(node, (xmlChar *)"value");
	if (!value)
		return;

	if (strstr((const char *)value, VIP_PROGRAMMER_MARKER))
		qdl->vip_data.programmer_requires_vip = true;

	xmlFree(value);
}

static void xml_setpropf(xmlNode *node, const char *attr, const char *fmt, ...)
{
	xmlChar buf[128];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf((char *)buf, sizeof(buf), fmt, ap);
	xmlSetProp(node, (xmlChar *)attr, buf);
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
		if (xmlStrcmp(node->name, (xmlChar *)"data") == 0)
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

static int firehose_generic_parser(xmlNode *node, void *data __unused, bool *rawmode)
{
	xmlChar *value;
	int ret = -EINVAL;

	value = xmlGetProp(node, (xmlChar *)"value");
	if (!value)
		return -EINVAL;

	if (xmlStrcmp(node->name, (xmlChar *)"log") == 0) {
		ux_log("LOG: %s\n", value);
		ret = -EAGAIN;
	} else if (xmlStrcmp(value, (xmlChar *)"ACK") == 0) {
		ret = FIREHOSE_ACK;
	} else if (xmlStrcmp(value, (xmlChar *)"NAK") == 0) {
		ret = FIREHOSE_NAK;
	}

	xmlFree(value);

	value = xmlGetProp(node, (xmlChar *)"rawmode");
	if (value) {
		if (xmlStrcmp(value, (xmlChar *)"true") == 0)
			*rawmode = true;
		xmlFree(value);
	}

	return ret;
}

/*
 * Scan @str for a contiguous run of exactly SHA256_DIGEST_STRING_LENGTH-1
 * (64) lowercase or uppercase hex characters bounded by a non-hex character
 * or string boundary. On match, decode into @digest and return true.
 */
static bool extract_sha256_hex(const char *str, uint8_t digest[SHA256_DIGEST_LENGTH])
{
	const size_t hex_len = SHA256_DIGEST_LENGTH * 2;
	const char *p = str;
	size_t i;

	while (*p) {
		size_t run = 0;
		const char *start;

		while (*p && !isxdigit((unsigned char)*p))
			p++;

		start = p;
		while (isxdigit((unsigned char)*p)) {
			run++;
			p++;
		}

		if (run == hex_len) {
			for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
				char byte[3] = { start[i * 2], start[i * 2 + 1], '\0' };

				digest[i] = (uint8_t)strtoul(byte, NULL, 16);
			}
			return true;
		}
	}

	return false;
}

static int firehose_sha256_parser(xmlNode *node, void *data, bool *rawmode)
{
	struct firehose_op *op = data;
	xmlChar *value;
	int ret;

	if (xmlStrcmp(node->name, (xmlChar *)"log") == 0) {
		value = xmlGetProp(node, (xmlChar *)"value");
		if (!value)
			return -EINVAL;

		if (!op->digest_valid && extract_sha256_hex((const char *)value, op->digest))
			op->digest_valid = true;

		ux_log("LOG: %s\n", value);
		xmlFree(value);
		return -EAGAIN;
	}

	ret = firehose_generic_parser(node, NULL, rawmode);

	/*
	 * Some Firehose implementations attach the digest as an attribute on
	 * the response itself rather than emitting it via <log>. Try a few
	 * known attribute names.
	 */
	if (!op->digest_valid) {
		static const char * const attrs[] = { "Digest", "SHA256", "sha256" };
		size_t i;

		for (i = 0; i < ARRAY_SIZE(attrs); i++) {
			value = xmlGetProp(node, (xmlChar *)attrs[i]);
			if (!value)
				continue;
			if (extract_sha256_hex((const char *)value, op->digest))
				op->digest_valid = true;
			xmlFree(value);
			if (op->digest_valid)
				break;
		}
	}

	return ret;
}

static int firehose_read(struct qdl_device *qdl, int timeout_ms,
			 int (*response_parser)(xmlNode *node, void *data, bool *rawmode),
			 void *data)
{
	char buf[4096];
	xmlNode *node;
	int error;
	int resp = -EIO;
	int ret = -EAGAIN;
	int n;
	bool rawmode = false;
	struct timeval timeout;
	struct timeval now;
	struct timeval delta = { .tv_sec = timeout_ms / 1000,
				 .tv_usec = (timeout_ms % 1000) * 1000 };

	gettimeofday(&now, NULL);
	timeradd(&now, &delta, &timeout);

	/*
	 * The goal of firehose_read() is to find a response to a request among
	 * one or more incoming messages AND to consume all incoming messages
	 * (otherwise subsequent writes will time out).
	 * The messages can be one of:
	 * - <log/>
	 * - <response value=""/>
	 * - <response value="" rawmode="true"/>
	 *
	 * Generally <log/> messages are coming prior to the <response/>, but
	 * on MSM8916 (at least) it's been observed that <log/> messages can
	 * arrive after the <response/>.
	 *
	 * We therefor need to consume messages until there are no more
	 * (timeout) and we have been able to parse out a response (using
	 * @response_parser).
	 *
	 * In the special case that the <response/> contain an attribute
	 * "rawmode=true", the device signals that it has entered a mode where
	 * it will not send/receive XML-formatted commands. So, (at least for
	 * reads) we need to shortcircuit the logic and directly terminate the
	 * consumption of incoming data.
	 */
	for (;;) {
		n = qdl_read(qdl, buf, sizeof(buf), 100);

		/* Timeout after seeing a response, we're done waiting for logs */
		if (n == -ETIMEDOUT && resp >= 0)
			break;
		/* We want to return resp on error, to not lose the reset response */
		else if (n == -EIO)
			break;

		if (n == -ETIMEDOUT || n == 0) {
			gettimeofday(&now, NULL);
			if (timercmp(&now, &timeout, <))
				continue;

			return -ETIMEDOUT;
		}
		buf[n] = '\0';

		ux_debug("FIREHOSE READ: %s\n", buf);

		/*
		 * On stream-oriented transports (Windows COM port via the
		 * QDLoader driver, virtio-console, ...) a single read can
		 * deliver multiple back-to-back Firehose responses
		 * concatenated, since the driver doesn't preserve USB bulk-
		 * transfer boundaries. Walk the buffer using the "<?xml" ...
		 * "</data>" envelope to bound each message; the closing tag
		 * is what really delimits the document so that any rawmode
		 * binary payload that arrives spliced onto the same read
		 * doesn't end up fed into libxml2 as if it were XML.
		 *
		 * libusb preserves transfer boundaries, so on that path each
		 * read still contains exactly one document and the loop runs
		 * once.
		 */
		char *cursor = buf;
		char *bufend = buf + n;

		while (cursor < bufend) {
			char *start = strstr(cursor, "<?xml");
			char *xml_end;
			size_t chunk;

			if (!start)
				break;

			/*
			 * Bound the XML on the closing </data> tag. If it's
			 * missing the message was either truncated or doesn't
			 * fit the schema we know how to parse; hand the rest
			 * of the buffer to libxml2 and let it error out
			 * gracefully.
			 */
			xml_end = strstr(start, "</data>");
			if (xml_end) {
				xml_end += sizeof("</data>") - 1;
				chunk = (size_t)(xml_end - start);
			} else {
				chunk = (size_t)(bufend - start);
			}

			node = firehose_response_parse(start, chunk, &error);
			if (!node)
				return error;

			firehose_check_vip_marker(qdl, node);

			ret = response_parser(node, data, &rawmode);
			xmlFreeDoc(node->doc);

			if (ret >= 0)
				resp = ret;

			cursor = start + chunk;

			/*
			 * The response we just parsed told the host to switch
			 * to raw mode (e.g. the ACK that precedes the binary
			 * sectors of a <read>). On a stream transport the
			 * first chunk of that binary payload can have arrived
			 * tacked onto this same read. Push it back so the
			 * next qdl_read() picks it up before the transport
			 * is touched again.
			 */
			if (rawmode) {
				if (cursor < bufend)
					qdl_push_back(qdl, cursor,
						      (size_t)(bufend - cursor));
				break;
			}
		}

		if (rawmode)
			break;
	}

	return resp;
}

static int firehose_vip_send_table(struct qdl_device *qdl)
{
	int ret;

	ret = vip_transfer_handle_tables(qdl);
	if (ret) {
		ux_err("VIP: error occurred during VIP table transmission\n");
		return -1;
	}

	if (!vip_transfer_status_check_needed(qdl))
		return 0;

	ret = firehose_read(qdl, 30000, firehose_generic_parser, NULL);
	if (ret) {
		ux_err("VIP: sending of digest table failed\n");
		return -1;
	}

	ux_info("VIP: digest table has been sent successfully\n");

	vip_transfer_clear_status(qdl);

	return 0;
}

static int firehose_write(struct qdl_device *qdl, xmlDoc *doc)
{
	int saved_errno;
	xmlChar *s;
	int len;
	int ret;

	xmlDocDumpMemory(doc, &s, &len);

	ret = firehose_vip_send_table(qdl);
	if (ret)
		return -1;

	vip_gen_chunk_init(qdl);

	for (;;) {
		ux_debug("FIREHOSE WRITE: %s\n", s);
		vip_gen_chunk_update(qdl, s, len);
		ret = qdl_write(qdl, s, len, 1000);
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
	vip_gen_chunk_store(qdl);
	return ret < 0 ? -saved_errno : 0;
}

/**
 * firehose_configure_response_parser() - parse a configure response
 * @node:	response xmlNode
 *
 * Return: max size supported by the remote, or negative errno on failure
 */
static int firehose_configure_response_parser(xmlNode *node, void *data,
					      bool *rawmode __unused)
{
	xmlChar *payload;
	xmlChar *value;
	size_t max_size;

	value = xmlGetProp(node, (xmlChar *)"value");
	if (!value)
		return -EINVAL;

	if (xmlStrcmp(node->name, (xmlChar *)"log") == 0) {
		ux_log("LOG: %s\n", value);
		xmlFree(value);
		return -EAGAIN;
	}

	payload = xmlGetProp(node, (xmlChar *)"MaxPayloadSizeToTargetInBytes");
	if (!payload) {
		xmlFree(value);
		return -EINVAL;
	}

	max_size = strtoul((char *)payload, NULL, 10);
	xmlFree(payload);

	/*
	 * When receiving an ACK the remote may indicate that we should attempt
	 * a larger payload size
	 */
	if (!xmlStrcmp(value, (xmlChar *)"ACK")) {
		payload = xmlGetProp(node, (xmlChar *)"MaxPayloadSizeToTargetInBytesSupported");
		if (!payload)
			return -EINVAL;

		max_size = strtoul((char *)payload, NULL, 10);
		xmlFree(payload);
	}

	*(size_t *)data = max_size;
	xmlFree(value);

	return FIREHOSE_ACK;
}

static int firehose_send_configure(struct qdl_device *qdl, size_t payload_size,
				   bool skip_storage_init,
				   enum qdl_storage_type storage,
				   size_t *max_payload_size)
{
	const char *memory_name;
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;

	memory_name = encode_storage_type(storage);
	if (!memory_name)
		return -EINVAL;

	doc = xmlNewDoc((xmlChar *)"1.0");
	root = xmlNewNode(NULL, (xmlChar *)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar *)"configure", NULL);
	xml_setpropf(node, "MemoryName", memory_name);
	xml_setpropf(node, "MaxPayloadSizeToTargetInBytes", "%lu", payload_size);
	xml_setpropf(node, "Verbose", "%d", 0);
	xml_setpropf(node, "ZlpAwareHost", "%d", 1);
	xml_setpropf(node, "SkipStorageInit", "%d", skip_storage_init);

	firehose_write(qdl, doc);
	xmlFreeDoc(doc);

	return firehose_read(qdl, 100, firehose_configure_response_parser, max_payload_size);
}

static int firehose_try_configure(struct qdl_device *qdl, bool skip_storage_init,
				  enum qdl_storage_type storage)
{
	size_t max_sector_size;
	size_t sector_sizes[] = { 512, 4096 };
	struct firehose_op op;
	size_t size = 0;
	void *buf;
	int ret;
	unsigned int i;

	ret = firehose_send_configure(qdl, qdl->max_payload_size, skip_storage_init,
				      storage, &size);
	if (ret < 0)
		return ret;

	/* Retry if remote proposed different size */
	if (size != qdl->max_payload_size) {
		ret = firehose_send_configure(qdl, size, skip_storage_init, storage, &size);
		if (ret != FIREHOSE_ACK) {
			ux_err("configure request with updated payload size failed\n");
			return -1;
		}

		qdl->max_payload_size = size;
	}

	ux_debug("accepted max payload size: %zu\n", qdl->max_payload_size);

	/*
	 * Skip sector size probing when VIP is active: the probe read commands
	 * are not included in the pre-built VIP digest table (the dry-run that
	 * builds it exits before reaching this code via the SIM early-return
	 * above), so sending them would cause a VIP hash mismatch on the device.
	 */
	if (storage != QDL_STORAGE_NAND && qdl->vip_data.state == VIP_DISABLED &&
	    !qdl->sector_size) {
		max_sector_size = sector_sizes[ARRAY_SIZE(sector_sizes) - 1];
		buf = alloca(max_sector_size);

		memset(&op, 0, sizeof(op));
		op.partition = 0;
		op.start_sector = "1";
		op.num_sectors = 1;

		/*
		 * Testing has shown that the loader will fail gracefully if a
		 * read is issued with the wrong sector size, use this to attempt
		 * to discover the storage device's sector size.
		 */
		for (i = 0; i < ARRAY_SIZE(sector_sizes); i++) {
			op.sector_size = sector_sizes[i];

			ret = firehose_read_buf(qdl, &op, buf, max_sector_size);
			if (ret == 0) {
				qdl->sector_size = sector_sizes[i];
				break;
			}
		}
	}

	if (qdl->sector_size)
		ux_debug("detected sector size of: %zd\n", qdl->sector_size);

	return 0;
}

static int firehose_erase(struct qdl_device *qdl, struct firehose_op *program)
{
	unsigned int sector_size;
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	sector_size = program->sector_size ? : qdl->sector_size;

	doc = xmlNewDoc((xmlChar *)"1.0");
	root = xmlNewNode(NULL, (xmlChar *)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar *)"erase", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", sector_size);
	xml_setpropf(node, "physical_partition_number", "%d", program->partition);

	/*
	 * Omitting num_sectors and start_sector attributes tells the programmer
	 * to erase the full physical partition.
	 */
	if (program->num_sectors > 0) {
		xml_setpropf(node, "num_partition_sectors", "%d", program->num_sectors);
		xml_setpropf(node, "start_sector", "%s", program->start_sector);
	}
	if (qdl->slot != UINT_MAX) {
		xml_setpropf(node, "slot", "%u", qdl->slot);
	}
	if (program->is_nand) {
		xml_setpropf(node, "PAGES_PER_BLOCK", "%d", program->pages_per_block);
	}

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

static int firehose_getsha256digest(struct qdl_device *qdl, struct firehose_op *op);

/*
 * Skipblock compares the bytes that would be written against the digest
 * the device reports for the same flash region and, on a match, avoids
 * rewriting them. Rather than digest the whole <program> region in one
 * <getsha256digest> - which on a multi-GB partition like rootfs reads the
 * entire region on the device and easily blows past
 * firehose_getsha256digest()'s read timeout - the region is walked one
 * bounded chunk at a time. Each chunk is hashed locally, compared against
 * the device digest for that sub-region, and reflashed on its own only when
 * it differs.
 *
 * Restricted to non-sparse, non-NAND programs with VIP disabled:
 *   - Sparse chunks would each need their own digest; v1 keeps them on
 *     the normal program path.
 *   - NAND has spare/OOB bytes whose semantics differ across
 *     programmers.
 *   - <getsha256digest> is not part of pre-built VIP digest tables.
 */
#define SKIPBLOCK_CHUNK_BYTES (512ULL * 1024 * 1024)	/* 512 MiB */

static bool firehose_skipblock_enabled(struct qdl_device *qdl,
				       struct firehose_op *program)
{
	return qdl->skipblock_mode == QDL_SKIPBLOCK_SHA256 &&
	       !program->sparse &&
	       !program->is_nand &&
	       qdl->vip_data.state == VIP_DISABLED;
}

/*
 * SHA-256 the @region_bytes of @file starting at @file_byte_off into @out.
 * Mirrors the program path's trailing zero-pad (see the memset() of the
 * residue in firehose_program_raw_region()): bytes past EOF are hashed as
 * zeros so a chunk that is short, or read from a non-zero file offset,
 * still produces a digest that can match flash. @buf is caller-owned
 * scratch of qdl->max_payload_size. Returns -1 on read error.
 */
static int firehose_region_local_digest(struct qdl_device *qdl,
					struct qdl_file *file,
					off_t file_byte_off,
					size_t region_bytes,
					void *buf,
					uint8_t out[SHA256_DIGEST_LENGTH])
{
	size_t hashed = 0;
	SHA2_CTX ctx;
	ssize_t hn;

	SHA256Init(&ctx);
	qdl_file_seek(file, file_byte_off, SEEK_SET);

	while (hashed < region_bytes) {
		size_t want = MIN(qdl->max_payload_size, region_bytes - hashed);

		hn = qdl_file_read_exact(file, buf, want);
		if (hn < 0)
			return -1;
		if (hn > 0) {
			SHA256Update(&ctx, buf, hn);
			hashed += (size_t)hn;
		}
		if ((size_t)hn < want)
			break;	/* short read == EOF, remainder is zero-pad */
	}

	if (hashed < region_bytes) {
		memset(buf, 0, qdl->max_payload_size);
		while (hashed < region_bytes) {
			size_t pad = MIN(qdl->max_payload_size, region_bytes - hashed);

			SHA256Update(&ctx, buf, pad);
			hashed += pad;
		}
	}

	SHA256Final(out, &ctx);
	return 0;
}

/*
 * Program the contiguous raw region [@start_sector, @start_sector +
 * @num_sectors) from the current position of @file. A self-contained
 * mirror of the non-sparse streaming in firehose_program(), used by the
 * skipblock fast-path to reflash one sub-region at a time. @file and @buf
 * (scratch of qdl->max_payload_size) are owned by the caller. Skipblock
 * requires VIP disabled, so the vip_*() calls below are no-ops kept for
 * parity with the main path.
 */
static int firehose_program_raw_region(struct qdl_device *qdl,
				       struct firehose_op *program,
				       struct qdl_file *file,
				       const char *start_sector,
				       unsigned int num_sectors,
				       unsigned int sector_size,
				       void *buf,
				       unsigned int zlp_timeout)
{
	size_t chunk_size;
	size_t left;
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;
	int n;

	doc = xmlNewDoc((xmlChar *)"1.0");
	root = xmlNewNode(NULL, (xmlChar *)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar *)"program", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", sector_size);
	xml_setpropf(node, "num_partition_sectors", "%d", num_sectors);
	xml_setpropf(node, "physical_partition_number", "%d", program->partition);
	xml_setpropf(node, "start_sector", "%s", start_sector);
	if (qdl->slot != UINT_MAX)
		xml_setpropf(node, "slot", "%u", qdl->slot);
	if (program->filename)
		xml_setpropf(node, "filename", "%s", program->filename);

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

	left = num_sectors;
	while (left > 0) {
		vip_gen_chunk_init(qdl);
		chunk_size = MIN(qdl->max_payload_size / sector_size, left);

		n = qdl_file_read_exact(file, buf, chunk_size * sector_size);
		if (n < 0) {
			ux_err("failed to read %s\n", program->filename);
			ret = -1;
			goto out;
		}
		if ((size_t)n < chunk_size * sector_size)
			memset(buf + n, 0, chunk_size * sector_size - n);

		vip_gen_chunk_update(qdl, buf, chunk_size * sector_size);

		ret = firehose_vip_send_table(qdl);
		if (ret) {
			ret = -1;
			goto out;
		}

		n = qdl_write(qdl, buf, chunk_size * sector_size, zlp_timeout);
		if (n < 0) {
			ux_err("USB write failed for data chunk\n");
			ret = firehose_read(qdl, 30000, firehose_generic_parser, NULL);
			if (ret)
				ux_err("flashing of chunk failed\n");
			ret = -1;
			goto out;
		}

		if ((size_t)n != chunk_size * sector_size) {
			ux_err("USB write truncated\n");
			ret = -1;
			goto out;
		}

		left -= chunk_size;
		vip_gen_chunk_store(qdl);

		ux_progress("%s", num_sectors - left, num_sectors, program->label);
	}

	ret = firehose_read(qdl, 120000, firehose_generic_parser, NULL);
	if (ret != FIREHOSE_ACK) {
		ux_err("flashing of %s failed\n", program->label);
		ret = -1;
		goto out;
	}

	ret = 0;
out:
	xmlFreeDoc(doc);
	return ret;
}

/*
 * Walk the @program region in SKIPBLOCK_CHUNK_BYTES chunks. For each chunk
 * compare the locally computed digest against the device's digest for the
 * matching flash sub-region; flash just that chunk when they differ, leave
 * it untouched when they match. Returns 0 on success, -1 on a flashing
 * failure. @buf is caller-owned scratch of qdl->max_payload_size.
 */
static int firehose_program_skipblock(struct qdl_device *qdl,
				      struct firehose_op *program,
				      struct qdl_file *file,
				      unsigned int num_sectors,
				      unsigned int sector_size,
				      void *buf,
				      unsigned int zlp_timeout)
{
	unsigned int chunk_max = SKIPBLOCK_CHUNK_BYTES / sector_size;
	uint64_t base = strtoull(program->start_sector, NULL, 0);
	unsigned int nchunks = num_sectors / chunk_max +
			       !!(num_sectors % chunk_max);
	bool split = nchunks > 1;
	unsigned int skipped = 0;
	unsigned int flashed = 0;
	unsigned int idx = 0;
	unsigned int off;

	for (off = 0; off < num_sectors; off += chunk_max, idx++) {
		unsigned int chunk_sectors = MIN(chunk_max, num_sectors - off);
		size_t region_bytes = (size_t)chunk_sectors * sector_size;
		off_t file_byte_off = (off_t)(program->file_offset + off) * sector_size;
		uint8_t local_digest[SHA256_DIGEST_LENGTH];
		struct firehose_op digest_op;
		const char *chunk_start;
		char start_sector[24];
		char chunk_id[32];
		bool match = false;

		/*
		 * start_sector is a firehose expression that may be symbolic
		 * (e.g. "NUM_DISK_SECTORS-33."), so the first chunk must reuse
		 * the original string verbatim - parsing it would mis-address
		 * the write. Only subsequent chunks of a split region need an
		 * offset, and those regions always carry a numeric start.
		 */
		if (off == 0) {
			chunk_start = program->start_sector;
		} else {
			snprintf(start_sector, sizeof(start_sector), "%llu",
				 (unsigned long long)(base + off));
			chunk_start = start_sector;
		}

		/*
		 * A region that fits in a single chunk keeps the original
		 * unqualified wording; only split regions name the chunk.
		 */
		if (split)
			snprintf(chunk_id, sizeof(chunk_id), " chunk %u of %u",
				 idx + 1, nchunks);
		else
			chunk_id[0] = '\0';

		ux_info("hashing \"%s\"%s locally (%zu KiB)...\n",
			program->label, chunk_id, region_bytes >> 10);

		if (firehose_region_local_digest(qdl, file, file_byte_off,
						 region_bytes, buf,
						 local_digest) == 0) {
			digest_op = (struct firehose_op){
				.type = FIREHOSE_OP_GET_SHA256_DIGEST,
				.sector_size = sector_size,
				.num_sectors = chunk_sectors,
				.partition = program->partition,
				.start_sector = chunk_start,
			};

			ux_info("requesting flash digest for \"%s\"%s...\n",
				program->label, chunk_id);

			if (firehose_getsha256digest(qdl, &digest_op) == 0 &&
			    !memcmp(local_digest, digest_op.digest,
				    SHA256_DIGEST_LENGTH))
				match = true;
		}

		if (match) {
			ux_info("skipped \"%s\"%s (sha256 match)\n",
				program->label, chunk_id);
			skipped++;
			continue;
		}

		ux_info("sha256 mismatch for \"%s\"%s, flashing\n",
			program->label, chunk_id);

		qdl_file_seek(file, file_byte_off, SEEK_SET);
		if (firehose_program_raw_region(qdl, program, file, chunk_start,
						chunk_sectors, sector_size, buf,
						zlp_timeout) < 0)
			return -1;

		flashed++;
	}

	if (split)
		ux_info("\"%s\": %u chunk(s) flashed, %u skipped\n",
			program->label, flashed, skipped);

	return 0;
}

static int firehose_program(struct qdl_device *qdl, struct firehose_op *program)
{
	unsigned int num_sectors;
	unsigned int sector_size;
	unsigned int zlp_timeout = 10000;
	struct qdl_file file;
	size_t chunk_size;
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	void *buf;
	time_t t0;
	time_t t;
	size_t left;
	int ret;
	int n;
	size_t i;
	uint32_t fill_value;

	/*
	 * ZLP has been measured to take up to 15 seconds on SPINOR devices,
	 * let's double it to be on the safe side...
	 */
	if (qdl->current_storage_type == QDL_STORAGE_SPINOR)
		zlp_timeout = 60000;

	if (!program->filename)
		return 0;

	ret = qdl_file_open(program->zip, program->filename, &file);
	if (ret < 0) {
		ux_err("unable to open %s\n", program->filename);
		return -1;
	}

	num_sectors = program->num_sectors;
	sector_size = program->sector_size ? : qdl->sector_size;

	if (!program->sparse) {
		num_sectors = (qdl_file_getsize(&file) + sector_size - 1) / sector_size;

		if (program->num_sectors && num_sectors > program->num_sectors) {
			ux_err("%s too big for %s truncated to %d\n",
			       program->filename,
			       program->label,
			       program->num_sectors * sector_size);
			num_sectors = program->num_sectors;
		}
	}

	buf = malloc(qdl->max_payload_size);
	if (!buf) {
		ux_err("failed to allocate sector buffer\n");
		goto err_close_fd;
	}

	if (firehose_skipblock_enabled(qdl, program)) {
		ret = firehose_program_skipblock(qdl, program, &file, num_sectors,
						 sector_size, buf, zlp_timeout);
		free(buf);
		qdl_file_close(&file);
		return ret;
	}

	doc = xmlNewDoc((xmlChar *)"1.0");
	root = xmlNewNode(NULL, (xmlChar *)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar *)"program", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", sector_size);
	xml_setpropf(node, "num_partition_sectors", "%d", num_sectors);
	xml_setpropf(node, "physical_partition_number", "%d", program->partition);
	xml_setpropf(node, "start_sector", "%s", program->start_sector);
	if (qdl->slot != UINT_MAX) {
		xml_setpropf(node, "slot", "%u", qdl->slot);
	}
	if (program->filename)
		xml_setpropf(node, "filename", "%s", program->filename);

	if (program->is_nand) {
		xml_setpropf(node, "PAGES_PER_BLOCK", "%d", program->pages_per_block);
		xml_setpropf(node, "last_sector", "%d", program->last_sector);
	}

	ret = firehose_write(qdl, doc);
	if (ret < 0) {
		ux_err("failed to send program request\n");
		goto err_free_doc;
	}

	ret = firehose_read(qdl, 10000, firehose_generic_parser, NULL);
	if (ret) {
		ux_err("failed to setup programming\n");
		goto err_free_doc;
	}

	t0 = time(NULL);

	if (!program->sparse) {
		qdl_file_seek(&file, (off_t)program->file_offset * sector_size, SEEK_SET);
	} else {
		switch (program->sparse_chunk_type) {
		case CHUNK_TYPE_RAW:
			qdl_file_seek(&file, program->sparse_offset, SEEK_SET);
			break;
		case CHUNK_TYPE_FILL:
			fill_value = program->sparse_fill_value;
			for (i = 0; i < qdl->max_payload_size; i += sizeof(fill_value))
				memcpy(buf + i, &fill_value, sizeof(fill_value));
			break;
		default:
			ux_err("[SPARSE] invalid chunk type\n");
			goto err_free_doc;
		}
	}

	left = num_sectors;

	ux_debug("FIREHOSE RAW BINARY WRITE: %s, %d bytes\n",
		 program->filename, sector_size * num_sectors);

	while (left > 0) {
		/*
		 * We should calculate hash for every raw packet sent,
		 * not for the whole binary.
		 */
		vip_gen_chunk_init(qdl);
		chunk_size = MIN(qdl->max_payload_size / sector_size, left);

		if (!program->sparse || program->sparse_chunk_type != CHUNK_TYPE_FILL) {
			n = qdl_file_read_exact(&file, buf, chunk_size * sector_size);
			if (n < 0) {
				ux_err("failed to read %s\n", program->filename);
				goto err_free_doc;
			}

			/*
			 * qdl_file_read_exact() only returns short on true
			 * EOF. The wire protocol expects exactly
			 * chunk_size * sector_size bytes, so zero-pad the
			 * residue (which is at most the trailing partial
			 * sector of the file).
			 */
			if ((size_t)n < chunk_size * sector_size)
				memset(buf + n, 0, chunk_size * sector_size - n);
		}

		vip_gen_chunk_update(qdl, buf, chunk_size * sector_size);

		ret = firehose_vip_send_table(qdl);
		if (ret)
			return -1;

		n = qdl_write(qdl, buf, chunk_size * sector_size, zlp_timeout);
		if (n < 0) {
			ux_err("USB write failed for data chunk\n");
			ret = firehose_read(qdl, 30000, firehose_generic_parser, NULL);
			if (ret)
				ux_err("flashing of chunk failed\n");

			goto err_free_doc;
		}

		if ((size_t)n != chunk_size * sector_size) {
			ux_err("USB write truncated\n");
			ret = -1;
			goto err_free_doc;
		}

		left -= chunk_size;
		vip_gen_chunk_store(qdl);

		ux_progress("%s", num_sectors - left, num_sectors, program->label);
	}

	t = time(NULL) - t0;

	ret = firehose_read(qdl, 120000, firehose_generic_parser, NULL);
	if (ret != FIREHOSE_ACK) {
		ux_err("flashing of %s failed\n", program->label);
		ret = -1;
		goto err_free_doc;
	}

	if (t) {
		ux_info("flashed \"%s\" successfully at %lukB/s\n",
			program->label,
			(unsigned long)sector_size * num_sectors / t / 1024);
	} else {
		ux_info("flashed \"%s\" successfully\n",
			program->label);
	}

	xmlFreeDoc(doc);
	free(buf);
	qdl_file_close(&file);

	return 0;

err_free_doc:
	xmlFreeDoc(doc);
	free(buf);
err_close_fd:
	qdl_file_close(&file);

	return -1;
}

static int firehose_issue_read(struct qdl_device *qdl, struct firehose_op *read_op,
			       int fd, void *out_buf, size_t out_len, bool quiet)
{
	unsigned int sector_size;
	size_t chunk_size;
	size_t out_offset = 0;
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	void *buf;
	time_t t0;
	time_t t;
	size_t left;
	int ret;
	int n;

	buf = malloc(qdl->max_payload_size);
	if (!buf) {
		ux_err("failed to allocate sector buffer\n");
		return -1;
	}

	doc = xmlNewDoc((xmlChar *)"1.0");
	root = xmlNewNode(NULL, (xmlChar *)"data");
	xmlDocSetRootElement(doc, root);

	sector_size = read_op->sector_size ? : qdl->sector_size;

	node = xmlNewChild(root, NULL, (xmlChar *)"read", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", sector_size);
	xml_setpropf(node, "num_partition_sectors", "%d", read_op->num_sectors);
	xml_setpropf(node, "physical_partition_number", "%d", read_op->partition);
	xml_setpropf(node, "start_sector", "%s", read_op->start_sector);
	if (qdl->slot != UINT_MAX) {
		xml_setpropf(node, "slot", "%u", qdl->slot);
	}
	if (read_op->filename)
		xml_setpropf(node, "filename", "%s", read_op->filename);

	ret = firehose_write(qdl, doc);
	if (ret < 0) {
		ux_err("failed to send read command\n");
		goto out;
	}

	ret = firehose_read(qdl, 10000, firehose_generic_parser, NULL);
	if (ret) {
		if (!quiet)
			ux_err("failed to setup reading operation\n");
		goto out;
	}

	t0 = time(NULL);

	left = read_op->num_sectors;
	while (left > 0) {
		size_t want;
		size_t got;

		chunk_size = MIN(qdl->max_payload_size / sector_size, left);
		want = chunk_size * sector_size;

		/*
		 * Accumulate the chunk across qdl_read() calls. libusb usually
		 * delivers an entire bulk transfer in one shot, but stream
		 * transports (QUD's Windows COM port, virtio-console, ...) can
		 * fragment it - including the rawmode tail that firehose_read()
		 * pushed back from the same buffer as the ACK response.
		 */
		got = 0;
		while (got < want) {
			n = qdl_read(qdl, (char *)buf + got, want - got, 30000);
			if (n < 0) {
				ux_err("failed to read sector data\n");
				ret = -1;
				goto out;
			}
			/*
			 * A 0-byte return is not EOF on a USB bulk-in pipe: it's
			 * a zero-length packet. Windows libusb (WinUSB) surfaces
			 * the ZLP that the device sends when transitioning from
			 * the XML/ACK phase to the rawmode binary phase before
			 * the first sector chunk of a large <read>; Linux's
			 * usbfs path generally hides it. If qdl_read() really
			 * has nothing more to deliver it will eventually return
			 * -ETIMEDOUT, which is handled above.
			 */
			if (n == 0)
				continue;
			got += (size_t)n;
		}

		if (out_buf) {
			size_t copy = want;

			if (copy > out_len - out_offset)
				copy = out_len - out_offset;

			memcpy((char *)out_buf + out_offset, buf, copy);
			out_offset += copy;
		} else {
			n = write(fd, buf, want);

			if (n < 0 || (size_t)n != want) {
				ux_err("failed to write sector data\n");
				ret = -1;
				goto out;
			}
		}

		left -= chunk_size;

		if (!quiet)
			ux_progress("%s", read_op->num_sectors - left, read_op->num_sectors, read_op->filename);
	}

	ret = firehose_read(qdl, 10000, firehose_generic_parser, NULL);
	if (ret) {
		ux_err("read operation failed\n");
		goto out;
	}

	t = time(NULL) - t0;

	if (!quiet) {
		if (t) {
			ux_info("read \"%s\" successfully at %ldkB/s\n",
				read_op->filename,
				(unsigned long)sector_size * read_op->num_sectors / t / 1024);
		} else {
			ux_info("read \"%s\" successfully\n",
				read_op->filename);
		}
	}

out:
	xmlFreeDoc(doc);
	free(buf);
	return ret;
}

int firehose_read_buf(struct qdl_device *qdl, struct firehose_op *read_op, void *out_buf, size_t out_size)
{
	return firehose_issue_read(qdl, read_op, -1, out_buf, out_size, true);
}

static int firehose_read_op(struct qdl_device *qdl, struct firehose_op *op)
{
	int ret;
	int fd;

	fd = open(op->filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
	if (fd < 0) {
		ux_info("unable to open %s...\n", op->filename);
		return -1;
	}

	ret = firehose_issue_read(qdl, op, fd, NULL, 0, false);

	close(fd);

	return ret;
}

/*
 * Issue a <getsha256digest> request and store the result on @op. On
 * success op->digest is populated and op->digest_valid is set; callers
 * that want to surface the digest (the qdl `sha256` subcommand prints
 * it after firehose_run() returns; the --skipblock=sha256 fast-path
 * compares it against a locally-computed digest) consume the field
 * themselves.
 */
static int firehose_getsha256digest(struct qdl_device *qdl, struct firehose_op *op)
{
	unsigned int sector_size;
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	sector_size = op->sector_size ? : qdl->sector_size;

	doc = xmlNewDoc((xmlChar *)"1.0");
	root = xmlNewNode(NULL, (xmlChar *)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar *)"getsha256digest", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", sector_size);
	xml_setpropf(node, "num_partition_sectors", "%d", op->num_sectors);
	xml_setpropf(node, "physical_partition_number", "%d", op->partition);
	xml_setpropf(node, "start_sector", "%s", op->start_sector);
	if (qdl->slot != UINT_MAX)
		xml_setpropf(node, "slot", "%u", qdl->slot);

	op->digest_valid = false;

	ret = firehose_write(qdl, doc);
	if (ret < 0) {
		ux_err("failed to send getsha256digest command\n");
		goto out;
	}

	ret = firehose_read(qdl, 30000, firehose_sha256_parser, op);
	if (ret != FIREHOSE_ACK) {
		ux_err("getsha256digest failed for %s+0x%x\n",
		       op->start_sector, op->num_sectors);
		ret = -1;
		goto out;
	}

	if (!op->digest_valid) {
		ux_err("getsha256digest returned no digest for %s+0x%x\n",
		       op->start_sector, op->num_sectors);
		ret = -1;
		goto out;
	}

	ret = 0;

out:
	xmlFreeDoc(doc);
	return ret;
}

static int firehose_apply_patch(struct qdl_device *qdl, struct firehose_op *patch)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	if (!patch->filename)
		return 0;

	if (strcmp(patch->filename, "DISK"))
		return 0;

	ux_debug("applying patch \"%s\"\n", patch->what);

	doc = xmlNewDoc((xmlChar *)"1.0");
	root = xmlNewNode(NULL, (xmlChar *)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar *)"patch", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", patch->sector_size);
	xml_setpropf(node, "byte_offset", "%d", patch->byte_offset);
	xml_setpropf(node, "filename", "%s", patch->filename);
	xml_setpropf(node, "physical_partition_number", "%d", patch->partition);
	xml_setpropf(node, "size_in_bytes", "%d", patch->size_in_bytes);
	xml_setpropf(node, "start_sector", "%s", patch->start_sector);
	xml_setpropf(node, "value", "%s", patch->value);
	if (qdl->slot != UINT_MAX) {
		xml_setpropf(node, "slot", "%u", qdl->slot);
	}

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

static int firehose_send_single_tag(struct qdl_device *qdl, xmlNode *node)
{
	xmlNode *root;
	xmlDoc *doc;
	int ret;

	doc = xmlNewDoc((xmlChar *)"1.0");
	root = xmlNewNode(NULL, (xmlChar *)"data");
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

	node_to_send = xmlNewNode(NULL, (xmlChar *)"ufs");

	xml_setpropf(node_to_send, "bNumberLU", "%d", ufs->bNumberLU);
	xml_setpropf(node_to_send, "bBootEnable", "%d", ufs->bBootEnable);
	xml_setpropf(node_to_send, "bDescrAccessEn", "%d", ufs->bDescrAccessEn);
	xml_setpropf(node_to_send, "bInitPowerMode", "%d", ufs->bInitPowerMode);
	xml_setpropf(node_to_send, "bHighPriorityLUN", "%d", ufs->bHighPriorityLUN);
	xml_setpropf(node_to_send, "bSecureRemovalType", "%d", ufs->bSecureRemovalType);
	xml_setpropf(node_to_send, "bInitActiveICCLevel", "%d", ufs->bInitActiveICCLevel);
	xml_setpropf(node_to_send, "wPeriodicRTCUpdate", "%d", ufs->wPeriodicRTCUpdate);
	xml_setpropf(node_to_send, "bConfigDescrLock", "%d", ufs->bConfigDescrLock);
	if (qdl->slot != UINT_MAX) {
		xml_setpropf(node_to_send, "slot", "%u", qdl->slot);
	}

	if (ufs->wb) {
		xml_setpropf(node_to_send, "bWriteBoosterBufferPreserveUserSpaceEn",
			     "%d", ufs->bWriteBoosterBufferPreserveUserSpaceEn);
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

	node_to_send = xmlNewNode(NULL, (xmlChar *)"ufs");

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
	if (qdl->slot != UINT_MAX) {
		xml_setpropf(node_to_send, "slot", "%u", qdl->slot);
	}
	if (ufs->desc)
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

	node_to_send = xmlNewNode(NULL, (xmlChar *)"ufs");

	xml_setpropf(node_to_send, "LUNtoGrow", "%d", ufs->LUNtoGrow);
	xml_setpropf(node_to_send, "commit", "%d", commit);
	if (qdl->slot != UINT_MAX) {
		xml_setpropf(node_to_send, "slot", "%u", qdl->slot);
	}

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

	doc = xmlNewDoc((xmlChar *)"1.0");
	root = xmlNewNode(NULL, (xmlChar *)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar *)"setbootablestoragedrive", NULL);
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

	doc = xmlNewDoc((xmlChar *)"1.0");
	root = xmlNewNode(NULL, (xmlChar *)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar *)"power", NULL);
	xml_setpropf(node, "value", "reset");
	xml_setpropf(node, "DelayInSeconds", "10"); // Add a delay to prevent reboot fail

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

static int firehose_detect_and_configure(struct qdl_device *qdl,
					 bool skip_storage_init,
					 enum qdl_storage_type storage,
					 unsigned int timeout_s)
{
	struct timeval timeout = { .tv_sec = timeout_s };
	struct timeval now;
	int ret;

	/* Track the currently configured storage type */
	qdl->current_storage_type = storage;

	/*
	 * The speculative retry loop below sends configure (and therefore the
	 * VIP table) before the programmer has had a chance to start up and
	 * request it.  The VIP table can only be sent once per session
	 * (VIP_INIT -> VIP_SEND_DATA is a one-way transition), so a premature
	 * send leaves the programmer waiting for a table that was already
	 * consumed, breaking the VIP session.
	 *
	 * When VIP is active, restore the original behaviour: drain all
	 * startup log messages first, then do a single configure attempt.
	 */
	if (qdl->vip_data.state != VIP_DISABLED) {
		firehose_read(qdl, timeout_s * 1000, firehose_generic_parser, NULL);

		/*
		 * The startup-log drain above is our only chance to learn
		 * whether the programmer expects a VIP table before configure
		 * is sent.  configure goes through firehose_write(), which
		 * unconditionally pushes the signed table when state is not
		 * VIP_DISABLED; if the programmer isn't in VIP mode it will
		 * try to parse those bytes as XML and reject the configure.
		 * Demote VIP here so the table is never sent.
		 */
		if (!qdl->vip_data.programmer_requires_vip) {
			ux_info("WARNING: --vip-table-path was provided but programmer did not announce VIP; continuing without VIP\n");
			qdl->vip_data.state = VIP_DISABLED;
		}

		ret = firehose_try_configure(qdl, false, storage);
		if (ret != FIREHOSE_ACK) {
			ux_err("configure request failed\n");
			return -1;
		}
		return 0;
	}

	gettimeofday(&now, NULL);
	timeradd(&now, &timeout, &timeout);
	for (;;) {
		ret = firehose_try_configure(qdl, skip_storage_init, storage);

		/*
		 * If the programmer's startup logs announced that VIP is
		 * active but no --vip-table-path was provided, bail out now
		 * rather than burning the full configure timeout waiting for
		 * a signed table that will never be sent.
		 */
		if (qdl->vip_data.programmer_requires_vip) {
			ux_err("programmer requires VIP, but no --vip-table-path was provided\n");
			return -1;
		}

		if (ret == FIREHOSE_ACK) {
			break;
		} else if (ret != -ETIMEDOUT) {
			ux_err("configure request failed\n");
			return -1;
		}

		gettimeofday(&now, NULL);
		if (timercmp(&now, &timeout, >)) {
			ux_err("failed to detect firehose programmer\n");
			return -1;
		}
	}

	return 0;
}

int firehose_provision(struct qdl_device *qdl, bool skip_reset)
{
	int ret;

	ret = firehose_detect_and_configure(qdl, true, QDL_STORAGE_UFS, 5);
	if (ret)
		return ret;

	ret = ufs_provisioning_execute(qdl, firehose_apply_ufs_common,
				       firehose_apply_ufs_body,
				       firehose_apply_ufs_epilogue);
	if (!ret)
		ux_info("UFS provisioning succeeded\n");
	else
		ux_info("UFS provisioning failed\n");

	if (!skip_reset)
		firehose_reset(qdl);

	return ret;

}

struct firehose_op *firehose_alloc_op(int type)
{
	struct firehose_op *op;

	op = calloc(1, sizeof(*op));
	if (!op)
		return NULL;

	op->type = type;
	return op;
}

void firehose_free_ops(struct list_head *ops)
{
	struct firehose_op *next;
	struct firehose_op *op;

	list_for_each_entry_safe(op, next, ops, node) {
		list_del(&op->node);
		qdl_zip_put(op->zip);
		free((void *)op->filename);
		free((void *)op->label);
		free((void *)op->start_sector);
		free((void *)op->gpt_partition);
		free((void *)op->value);
		free((void *)op->what);
		free(op);
	}
}

static int firehose_execute_ops(struct qdl_device *qdl, struct list_head *ops)
{
	unsigned int patch_count = 0;
	struct firehose_op *status_patch = NULL;
	struct firehose_op *tmp;
	struct firehose_op *op;
	unsigned int patch_idx = 0;
	int ret;

	list_for_each_entry(op, ops, node) {
		switch (op->type) {
		case FIREHOSE_OP_CONFIGURE:
			ret = firehose_detect_and_configure(qdl, false, op->storage_type, 5);
			if (ret)
				return ret;

			ret = gpt_resolve_deferrals(qdl, ops);
			if (ret)
				return ret;

			/* Update the number of patches for this storage device */
			patch_count = 0;
			patch_idx = 0;
			tmp = op;
			list_for_each_entry_continue(tmp, ops, node) {
				if (tmp->type == FIREHOSE_OP_CONFIGURE)
					break;
				if (tmp->type != FIREHOSE_OP_PATCH)
					continue;
				if (tmp->filename && !strcmp(tmp->filename, "DISK")) {
					patch_count++;
					status_patch = tmp;
				}
			}
			break;
		case FIREHOSE_OP_PROGRAM:
			ret = firehose_program(qdl, op);
			if (ret < 0)
				return ret;
			break;
		case FIREHOSE_OP_ERASE:
			ret = firehose_erase(qdl, op);
			if (ret < 0)
				return ret;
			break;
		case FIREHOSE_OP_READ:
			ret = firehose_read_op(qdl, op);
			if (ret < 0)
				return ret;
			break;
		case FIREHOSE_OP_GET_SHA256_DIGEST:
			ret = firehose_getsha256digest(qdl, op);
			if (ret < 0)
				return ret;
			break;
		case FIREHOSE_OP_PATCH:
			ret = firehose_apply_patch(qdl, op);
			if (ret)
				return ret;

			if (op->filename && !strcmp(op->filename, "DISK"))
				ux_progress("Applying patches", ++patch_idx, patch_count);

			if (op == status_patch)
				ux_info("%d patches applied\n", patch_idx);
			break;
		case FIREHOSE_OP_SET_BOOTABLE:
			firehose_set_bootable(qdl, op->partition);
			break;
		case FIREHOSE_OP_RESET:
			ret = firehose_reset(qdl);
			if (ret < 0)
				return ret;
			break;
		default:
			ux_err("internal error: unknown firehose operation %d\n", op->type);
			return -1;
		}
	}

	return 0;
}

int firehose_run(struct qdl_device *qdl, struct list_head *ops)
{
	ux_info("waiting for Firehose programmer...\n");

	return firehose_execute_ops(qdl, ops);
}
