// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * All rights reserved.
 */
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "qdl.h"
#include "oscompat.h"

#define SAHARA_HELLO_CMD		0x1  /* Min protocol version 1.0 */
#define SAHARA_HELLO_RESP_CMD		0x2  /* Min protocol version 1.0 */
#define SAHARA_READ_DATA_CMD		0x3  /* Min protocol version 1.0 */
#define SAHARA_END_OF_IMAGE_CMD		0x4  /* Min protocol version 1.0 */
#define SAHARA_DONE_CMD			0x5  /* Min protocol version 1.0 */
#define SAHARA_DONE_RESP_CMD		0x6  /* Min protocol version 1.0 */
#define SAHARA_RESET_CMD		0x7  /* Min protocol version 1.0 */
#define SAHARA_RESET_RESP_CMD		0x8  /* Min protocol version 1.0 */
#define SAHARA_MEM_DEBUG_CMD		0x9  /* Min protocol version 2.0 */
#define SAHARA_MEM_READ_CMD		0xa  /* Min protocol version 2.0 */
#define SAHARA_CMD_READY_CMD		0xb  /* Min protocol version 2.1 */
#define SAHARA_SWITCH_MODE_CMD		0xc  /* Min protocol version 2.1 */
#define SAHARA_EXECUTE_CMD		0xd  /* Min protocol version 2.1 */
#define SAHARA_EXECUTE_RESP_CMD		0xe  /* Min protocol version 2.1 */
#define SAHARA_EXECUTE_DATA_CMD		0xf  /* Min protocol version 2.1 */
#define SAHARA_MEM_DEBUG64_CMD		0x10 /* Min protocol version 2.5 */
#define SAHARA_MEM_READ64_CMD		0x11 /* Min protocol version 2.5 */
#define SAHARA_READ_DATA64_CMD		0x12 /* Min protocol version 2.8 */
#define SAHARA_RESET_STATE_CMD		0x13 /* Min protocol version 2.9 */
#define SAHARA_WRITE_DATA_CMD		0x14 /* Min protocol version 3.0 */

#define SAHARA_VERSION			2
#define SAHARA_SUCCESS			0

#define SAHARA_MODE_IMAGE_TX_PENDING	0x0
#define SAHARA_MODE_IMAGE_TX_COMPLETE	0x1
#define SAHARA_MODE_MEMORY_DEBUG	0x2
#define SAHARA_MODE_COMMAND		0x3

#define SAHARA_HELLO_LENGTH		0x30
#define SAHARA_READ_DATA_LENGTH		0x14
#define SAHARA_READ_DATA64_LENGTH	0x20
#define SAHARA_END_OF_IMAGE_LENGTH	0x10
#define SAHARA_MEM_READ64_LENGTH	0x18
#define SAHARA_MEM_DEBUG64_LENGTH	0x18
#define SAHARA_DONE_LENGTH		0x8
#define SAHARA_DONE_RESP_LENGTH		0xc
#define SAHARA_RESET_LENGTH		0x8

#define DEBUG_BLOCK_SIZE (512u * 1024u)

#define SAHARA_CMD_TIMEOUT_MS	1000

struct sahara_pkt {
	uint32_t cmd;
	uint32_t length;

	union {
		struct {
			uint32_t version;
			uint32_t compatible;
			uint32_t max_len;
			uint32_t mode;
			uint32_t reserved[6];
		} hello_req;
		struct {
			uint32_t version;
			uint32_t compatible;
			uint32_t status;
			uint32_t mode;
			uint32_t reserved[6];
		} hello_resp;
		struct {
			uint32_t image;
			uint32_t offset;
			uint32_t length;
		} read_req;
		struct {
			uint32_t image;
			uint32_t status;
		} eoi;
		struct {
		} done_req;
		struct {
			uint32_t status;
		} done_resp;
		struct {
			uint64_t addr;
			uint64_t length;
		} debug64_req;
		struct {
			uint64_t image;
			uint64_t offset;
			uint64_t length;
		} read64_req;
	};
};

struct sahara_debug_region64 {
	uint64_t type;
	uint64_t addr;
	uint64_t length;
	char region[20];
	char filename[20];
};

static void sahara_send_reset(struct qdl_device *qdl)
{
	struct sahara_pkt resp;

	resp.cmd = SAHARA_RESET_CMD;
	resp.length = SAHARA_RESET_LENGTH;

	qdl_write(qdl, &resp, resp.length, SAHARA_CMD_TIMEOUT_MS);
}

static void sahara_hello(struct qdl_device *qdl, struct sahara_pkt *pkt)
{
	struct sahara_pkt resp = {};

	assert(pkt->length == SAHARA_HELLO_LENGTH);

	ux_debug("HELLO version: 0x%x compatible: 0x%x max_len: %d mode: %d\n",
		 pkt->hello_req.version, pkt->hello_req.compatible, pkt->hello_req.max_len, pkt->hello_req.mode);

	resp.cmd = SAHARA_HELLO_RESP_CMD;
	resp.length = SAHARA_HELLO_LENGTH;
	resp.hello_resp.version = SAHARA_VERSION;
	resp.hello_resp.compatible = 1;
	resp.hello_resp.status = SAHARA_SUCCESS;
	resp.hello_resp.mode = pkt->hello_req.mode;

	qdl_write(qdl, &resp, resp.length, SAHARA_CMD_TIMEOUT_MS);
}

static void sahara_read(struct qdl_device *qdl, struct sahara_pkt *pkt,
			const struct sahara_image *images)
{
	const struct sahara_image *image;
	unsigned int image_idx;
	size_t offset;
	size_t len;
	int ret;

	assert(pkt->length == SAHARA_READ_DATA_LENGTH);

	ux_debug("READ image: %d offset: 0x%x length: 0x%x\n",
		 pkt->read_req.image, pkt->read_req.offset, pkt->read_req.length);

	image_idx = pkt->read_req.image;
	if (image_idx >= MAPPING_SZ || !images[image_idx].ptr) {
		ux_err("device requested unknown image id %u, ensure that all Sahara images are provided\n",
		       image_idx);
		sahara_send_reset(qdl);
		return;
	}

	offset = pkt->read_req.offset;
	len = pkt->read_req.length;

	image = &images[image_idx];
	if (offset > image->len || offset + len > image->len) {
		ux_err("device requested invalid range of image %d\n", image_idx);
		return;
	}

	ret = qdl_write(qdl, image->ptr + offset, len, SAHARA_CMD_TIMEOUT_MS);
	if (ret < 0 || ((size_t)ret != len))
		err(1, "failed to write %zu bytes to sahara", len);
}

static void sahara_read64(struct qdl_device *qdl, struct sahara_pkt *pkt,
			  const struct sahara_image *images)
{
	const struct sahara_image *image;
	unsigned int image_idx;
	size_t offset;
	size_t len;
	int ret;

	assert(pkt->length == SAHARA_READ_DATA64_LENGTH);

	ux_debug("READ64 image: %" PRId64 " offset: 0x%" PRIx64 " length: 0x%" PRIx64 "\n",
		 pkt->read64_req.image, pkt->read64_req.offset, pkt->read64_req.length);

	image_idx = pkt->read64_req.image;
	if (image_idx >= MAPPING_SZ || !images[image_idx].ptr) {
		ux_err("device requested unknown image id %u, ensure that all Sahara images are provided\n",
		       image_idx);
		sahara_send_reset(qdl);
		return;
	}

	offset = pkt->read64_req.offset;
	len = pkt->read64_req.length;

	image = &images[image_idx];
	if (offset > image->len || offset + len > image->len) {
		ux_err("device requested invalid range of image %d\n", image_idx);
		return;
	}

	ret = qdl_write(qdl, image->ptr + offset, len, SAHARA_CMD_TIMEOUT_MS);
	if (ret < 0 || ((size_t)ret != len))
		err(1, "failed to write %zu bytes to sahara", len);
}

static void sahara_eoi(struct qdl_device *qdl, struct sahara_pkt *pkt)
{
	struct sahara_pkt done;

	assert(pkt->length == SAHARA_END_OF_IMAGE_LENGTH);

	ux_debug("END OF IMAGE image: %d status: %d\n", pkt->eoi.image, pkt->eoi.status);

	if (pkt->eoi.status != 0) {
		ux_err("received non-successful end-of-image result\n");
		return;
	}

	done.cmd = SAHARA_DONE_CMD;
	done.length = SAHARA_DONE_LENGTH;
	qdl_write(qdl, &done, done.length, SAHARA_CMD_TIMEOUT_MS);
}

static int sahara_done(struct qdl_device *qdl __unused, struct sahara_pkt *pkt)
{
	assert(pkt->length == SAHARA_DONE_RESP_LENGTH);

	ux_debug("DONE status: %d\n", pkt->done_resp.status);

	// 0 == PENDING, 1 == COMPLETE.  Device expects more images if
	// PENDING is set in status.
	return pkt->done_resp.status;
}

static ssize_t sahara_debug64_one(struct qdl_device *qdl,
				  struct sahara_debug_region64 region,
				  const char *ramdump_path)
{
	struct sahara_pkt read_req;
	uint64_t remain;
	size_t offset, buf_offset;
	size_t chunk;
	size_t written;
	ssize_t n;
	void *buf;
	int fd;

	buf = malloc(DEBUG_BLOCK_SIZE);
	if (!buf)
		return -1;

	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", ramdump_path, region.filename);

	fd = open(path, O_WRONLY | O_CREAT | O_BINARY, 0644);
	if (fd < 0) {
		warn("failed to open \"%s\"", region.filename);
		free(buf);
		return -1;
	}

	chunk = 0;
	while (chunk < region.length) {
		remain = MIN((uint64_t)(region.length - chunk), DEBUG_BLOCK_SIZE);

		read_req.cmd = SAHARA_MEM_READ64_CMD;
		read_req.length = SAHARA_MEM_READ64_LENGTH;
		read_req.debug64_req.addr = region.addr + chunk;
		read_req.debug64_req.length = remain;
		n = qdl_write(qdl, &read_req, read_req.length, SAHARA_CMD_TIMEOUT_MS);
		if (n < 0)
			break;

		offset = 0;
		while (offset < remain) {
			buf_offset = 0;
			n = qdl_read(qdl, buf, DEBUG_BLOCK_SIZE, 30000);
			if (n < 0) {
				warn("failed to read ramdump chunk");
				goto out;
			}

			while (buf_offset < (size_t)n) {
				written = write(fd, buf + buf_offset, n - buf_offset);
				if (written <= 0) {
					warn("failed to write ramdump chunk to \"%s\"", region.filename);
					goto out;
				}
				buf_offset += written;
			}

			offset += buf_offset;
		}

		qdl_read(qdl, buf, DEBUG_BLOCK_SIZE, 10);

		chunk += DEBUG_BLOCK_SIZE;

		ux_progress("%s", chunk, region.length, region.filename);
	}
out:

	close(fd);
	free(buf);

	return 0;
}

// simple pattern matching function supporting * and ?
bool pattern_match(const char *pattern, const char *string)
{
	if (*pattern == '\0' && *string == '\0')
		return true;

	if (*pattern == '*')
		return pattern_match(pattern + 1, string) ||
		       (*string != '\0' && pattern_match(pattern, string + 1));

	if (*pattern == '?')
		return (*string != '\0') && pattern_match(pattern + 1, string + 1);

	if (*pattern == *string)
		return pattern_match(pattern + 1, string + 1);

	return false;
}

static bool sahara_debug64_filter(const char *filename, const char *filter)
{
	bool anymatch = false;
	char *ptr;
	char *tmp;
	char *s;

	if (!filter)
		return false;

	tmp = strdup(filter);
	for (s = strtok_r(tmp, ",", &ptr); s; s = strtok_r(NULL, ",", &ptr)) {
		if (pattern_match(s, filename)) {
			anymatch = true;
			break;
		}
	}
	free(tmp);

	return !anymatch;
}

static void sahara_debug64(struct qdl_device *qdl, struct sahara_pkt *pkt,
			   const char *ramdump_path, const char *filter)
{
	struct sahara_debug_region64 *table;
	struct sahara_pkt read_req;
	ssize_t n;
	size_t i;

	assert(pkt->length == SAHARA_MEM_DEBUG64_LENGTH);

	ux_debug("DEBUG64 address: 0x%" PRIx64 " length: 0x%" PRIx64 "\n",
		 pkt->debug64_req.addr, pkt->debug64_req.length);

	read_req.cmd = SAHARA_MEM_READ64_CMD;
	read_req.length = SAHARA_MEM_READ64_LENGTH;
	read_req.debug64_req.addr = pkt->debug64_req.addr;
	read_req.debug64_req.length = pkt->debug64_req.length;

	n = qdl_write(qdl, &read_req, read_req.length, SAHARA_CMD_TIMEOUT_MS);
	if (n < 0)
		return;

	table = malloc(read_req.debug64_req.length);

	n = qdl_read(qdl, table, pkt->debug64_req.length, SAHARA_CMD_TIMEOUT_MS);
	if (n < 0)
		return;

	for (i = 0; i < pkt->debug64_req.length / sizeof(table[0]); i++) {
		if (sahara_debug64_filter(table[i].filename, filter)) {
			ux_info("%s skipped per filter\n", table[i].filename);
			continue;
		}

		ux_debug("%-2d: type 0x%" PRIx64 " address: 0x%" PRIx64 " length: 0x%"
			 PRIx64 " region: %s filename: %s\n",
			 i, table[i].type, table[i].addr, table[i].length,
			 table[i].region, table[i].filename);

		n = sahara_debug64_one(qdl, table[i], ramdump_path);
		if (n < 0)
			break;

		ux_info("%s dumped successfully\n", table[i].filename);
	}

	free(table);

	sahara_send_reset(qdl);
}

static bool sahara_has_done_pending_quirk(const struct sahara_image *images)
{
	unsigned int count = 0;
	int i;

	/*
	 * E.g MSM8916 EDL reports done = pending, allow this when one a single
	 * image is provided, and it's used as SAHARA_ID_EHOSTDL_IMG.
	 */
	for (i = 0; i < MAPPING_SZ; i++) {
		if (images[i].ptr)
			count++;
	}

	return count == 1 && images[SAHARA_ID_EHOSTDL_IMG].ptr;
}

static void sahara_debug_list_images(const struct sahara_image *images)
{
	int i;

	ux_debug("Sahara images:\n");
	for (i = 0; i < MAPPING_SZ; i++) {
		if (images[i].ptr)
			ux_debug("  %2d: %s\n", i, images[i].name ? : "(unknown)");
	}
}

int sahara_run(struct qdl_device *qdl, const struct sahara_image *images,
	       const char *ramdump_path,
	       const char *ramdump_filter)
{
	struct sahara_pkt *pkt;
	char buf[4096];
	char tmp[32];
	bool done = false;
	int n;

	if (images)
		sahara_debug_list_images(images);

	/*
	 * Don't need to do anything in simulation mode with Sahara,
	 * we care only about Firehose protocol
	 */
	if (qdl->dev_type == QDL_DEVICE_SIM)
		return 0;

	while (!done) {
		n = qdl_read(qdl, buf, sizeof(buf), SAHARA_CMD_TIMEOUT_MS);
		if (n < 0) {
			ux_err("failed to read sahara request from device\n");
			break;
		}

		pkt = (struct sahara_pkt *)buf;
		if ((uint32_t)n != pkt->length) {
			ux_err("request length not matching received request\n");
			return -EINVAL;
		}

		switch (pkt->cmd) {
		case SAHARA_HELLO_CMD:
			sahara_hello(qdl, pkt);
			break;
		case SAHARA_READ_DATA_CMD:
			sahara_read(qdl, pkt, images);
			break;
		case SAHARA_END_OF_IMAGE_CMD:
			sahara_eoi(qdl, pkt);
			break;
		case SAHARA_DONE_RESP_CMD:
			done = sahara_done(qdl, pkt);

			/* E.g MSM8916 EDL reports done = 0 here */
			if (sahara_has_done_pending_quirk(images))
				done = true;
			break;
		case SAHARA_MEM_DEBUG64_CMD:
			sahara_debug64(qdl, pkt, ramdump_path, ramdump_filter);
			break;
		case SAHARA_READ_DATA64_CMD:
			sahara_read64(qdl, pkt, images);
			break;
		case SAHARA_RESET_RESP_CMD:
			assert(pkt->length == SAHARA_RESET_LENGTH);
			if (ramdump_path)
				done = true;
			break;
		default:
			sprintf(tmp, "CMD%x", pkt->cmd);
			print_hex_dump(tmp, buf, n);
			break;
		}
	}

	return done ? 0 : -1;
}
