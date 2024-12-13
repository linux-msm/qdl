/*
 * Copyright (c) 2016-2017, Linaro Ltd.
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
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <inttypes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "qdl.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))

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

#define DEBUG_BLOCK_SIZE (512*1024)

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

	qdl_write(qdl, &resp, resp.length);
}

static void sahara_hello(struct qdl_device *qdl, struct sahara_pkt *pkt)
{
	struct sahara_pkt resp = {};

	assert(pkt->length == SAHARA_HELLO_LENGTH);

	printf("HELLO version: 0x%x compatible: 0x%x max_len: %d mode: %d\n",
	       pkt->hello_req.version, pkt->hello_req.compatible, pkt->hello_req.max_len, pkt->hello_req.mode);

	resp.cmd = SAHARA_HELLO_RESP_CMD;
	resp.length = SAHARA_HELLO_LENGTH;
	resp.hello_resp.version = SAHARA_VERSION;
	resp.hello_resp.compatible = 1;
	resp.hello_resp.status = SAHARA_SUCCESS;
	resp.hello_resp.mode = pkt->hello_req.mode;

	qdl_write(qdl, &resp, resp.length);
}

static int sahara_read_common(struct qdl_device *qdl, int progfd, off_t offset, size_t len)
{
	ssize_t n;
	void *buf;
	int ret = 0;

	buf = malloc(len);
	if (!buf)
		return -ENOMEM;

	lseek(progfd, offset, SEEK_SET);
	n = read(progfd, buf, len);
	if (n != len) {
		ret = -errno;
		goto out;
	}

	n = qdl_write(qdl, buf, n);
	if (n != len)
		err(1, "failed to write %zu bytes to sahara", len);

	free(buf);

out:
	return ret;
}

static void sahara_read(struct qdl_device *qdl, struct sahara_pkt *pkt, char *img_arr[], bool single_image)
{
	unsigned int image;
	int ret;
	int fd;

	assert(pkt->length == SAHARA_READ_DATA_LENGTH);

	printf("READ image: %d offset: 0x%x length: 0x%x\n",
	       pkt->read_req.image, pkt->read_req.offset, pkt->read_req.length);

	if (single_image)
		image = 0;
	else
		image = pkt->read_req.image;

	if (image >= MAPPING_SZ || !img_arr[image]) {
		fprintf(stderr, "Device specified invalid image: %u\n", image);
		sahara_send_reset(qdl);
		return;
	}

	fd = open(img_arr[image], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can not open %s: %s\n", img_arr[image], strerror(errno));
		// Maybe this read was optional.  Notify device of error and let
		// it decide how to proceed.
		sahara_send_reset(qdl);
		return;
	}

	ret = sahara_read_common(qdl, fd, pkt->read_req.offset, pkt->read_req.length);
	if (ret < 0)
		errx(1, "failed to read image chunk to sahara");

	close(fd);
}

static void sahara_read64(struct qdl_device *qdl, struct sahara_pkt *pkt, char *img_arr[], bool single_image)
{
	unsigned int image;
	int ret;
	int fd;

	assert(pkt->length == SAHARA_READ_DATA64_LENGTH);

	printf("READ64 image: %" PRId64 " offset: 0x%" PRIx64 " length: 0x%" PRIx64 "\n",
	       pkt->read64_req.image, pkt->read64_req.offset, pkt->read64_req.length);

	if (single_image)
		image = 0;
	else
		image = pkt->read64_req.image;

	if (image >= MAPPING_SZ || !img_arr[image]) {
		fprintf(stderr, "Device specified invalid image: %u\n", image);
		sahara_send_reset(qdl);
		return;
	}
	fd = open(img_arr[image], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can not open %s: %s\n", img_arr[image], strerror(errno));
		// Maybe this read was optional.  Notify device of error and let
		// it decide how to proceed.
		sahara_send_reset(qdl);
		return;
	}

	ret = sahara_read_common(qdl, fd, pkt->read64_req.offset, pkt->read64_req.length);
	if (ret < 0)
		errx(1, "failed to read image chunk to sahara");

	close(fd);
}

static void sahara_eoi(struct qdl_device *qdl, struct sahara_pkt *pkt)
{
	struct sahara_pkt done;

	assert(pkt->length == SAHARA_END_OF_IMAGE_LENGTH);

	printf("END OF IMAGE image: %d status: %d\n", pkt->eoi.image, pkt->eoi.status);

	if (pkt->eoi.status != 0) {
		printf("received non-successful result\n");
		return;
	}

	done.cmd = SAHARA_DONE_CMD;
	done.length = SAHARA_DONE_LENGTH;
	qdl_write(qdl, &done, done.length);
}

static int sahara_done(struct qdl_device *qdl, struct sahara_pkt *pkt)
{
	assert(pkt->length == SAHARA_DONE_RESP_LENGTH);

	printf("DONE status: %d\n", pkt->done_resp.status);

	// 0 == PENDING, 1 == COMPLETE.  Device expects more images if
	// PENDING is set in status.
	return pkt->done_resp.status;
}

static ssize_t sahara_debug64_one(struct qdl_device *qdl,
				  struct sahara_debug_region64 region,
				  int ramdump_dir)
{
	struct sahara_pkt read_req;
	uint64_t remain;
	size_t offset;
	size_t chunk;
	ssize_t n;
	void *buf;
	int fd;

	buf = malloc(DEBUG_BLOCK_SIZE);
	if (!buf)
		return -1;

	fd = openat(ramdump_dir, region.filename, O_WRONLY | O_CREAT, 0644);
	if (fd < 0) {
		warn("failed to open \"%s\"", region.filename);
		return -1;
	}

	chunk = 0;
	while (chunk < region.length) {
		remain = MIN(region.length - chunk, DEBUG_BLOCK_SIZE);

		read_req.cmd = SAHARA_MEM_READ64_CMD;
		read_req.length = SAHARA_MEM_READ64_LENGTH;
		read_req.debug64_req.addr = region.addr + chunk;
		read_req.debug64_req.length = remain;
		n = qdl_write(qdl, &read_req, read_req.length);
		if (n < 0)
			break;

		offset = 0;
		while (offset < remain) {
			n = qdl_read(qdl, buf, DEBUG_BLOCK_SIZE, 30000);
			if (n < 0) {
				warn("failed to read ramdump chunk");
				goto out;
			}

			write(fd, buf, n);
			offset += n;
		}

		qdl_read(qdl, buf, DEBUG_BLOCK_SIZE, 10);

		chunk += DEBUG_BLOCK_SIZE;
	}
out:

	close(fd);
	free(buf);

	return 0;
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
		if (fnmatch(s, filename, 0) == 0) {
			anymatch = true;
			break;
		}
	}
	free(tmp);

	return !anymatch;
}

static void sahara_debug64(struct qdl_device *qdl, struct sahara_pkt *pkt,
			   int ramdump_dir, const char *filter)
{
	struct sahara_debug_region64 *table;
	struct sahara_pkt read_req;
	ssize_t n;
	int i;

	assert(pkt->length == SAHARA_MEM_DEBUG64_LENGTH);

	printf("DEBUG64 address: 0x%" PRIx64 " length: 0x%" PRIx64 "\n",
		pkt->debug64_req.addr, pkt->debug64_req.length);

	read_req.cmd = SAHARA_MEM_READ64_CMD;
	read_req.length = SAHARA_MEM_READ64_LENGTH;
	read_req.debug64_req.addr = pkt->debug64_req.addr;
	read_req.debug64_req.length = pkt->debug64_req.length;

	n = qdl_write(qdl, &read_req, read_req.length);
	if (n < 0)
		return;

	table = malloc(read_req.debug64_req.length);

	n = qdl_read(qdl, table, pkt->debug64_req.length, 1000);
	if (n < 0)
		return;

	for (i = 0; i < pkt->debug64_req.length / sizeof(table[0]); i++) {
		if (sahara_debug64_filter(table[i].filename, filter))
			continue;

		printf("%-2d: type 0x%" PRIx64 " address: 0x%" PRIx64 " length: 0x%" PRIx64 " region: %s filename: %s\n",
		       i, table[i].type, table[i].addr, table[i].length, table[i].region, table[i].filename);


		n = sahara_debug64_one(qdl, table[i], ramdump_dir);
		if (n < 0)
			break;

	}

	free(table);

	sahara_send_reset(qdl);
}

int sahara_run(struct qdl_device *qdl, char *img_arr[], bool single_image,
	       const char *ramdump_path, const char *ramdump_filter)
{
	struct sahara_pkt *pkt;
	int ramdump_dir = -1;
	char buf[4096];
	char tmp[32];
	bool done = false;
	int n;

	if (ramdump_path) {
		ramdump_dir = open(ramdump_path, O_DIRECTORY);
		if (ramdump_dir < 0)
			err(1, "failed to open directory for ramdump output");
	}

	while (!done) {
		n = qdl_read(qdl, buf, sizeof(buf), 1000);
		if (n < 0)
			break;

		pkt = (struct sahara_pkt*)buf;
		if (n != pkt->length) {
			fprintf(stderr, "length not matching\n");
			return -EINVAL;
		}

		switch (pkt->cmd) {
		case SAHARA_HELLO_CMD:
			sahara_hello(qdl, pkt);
			break;
		case SAHARA_READ_DATA_CMD:
			sahara_read(qdl, pkt, img_arr, single_image);
			break;
		case SAHARA_END_OF_IMAGE_CMD:
			sahara_eoi(qdl, pkt);
			break;
		case SAHARA_DONE_RESP_CMD:
			done = sahara_done(qdl, pkt);

			/* E.g MSM8916 EDL reports done = 0 here */
			if (single_image)
				done = true;
			break;
		case SAHARA_MEM_DEBUG64_CMD:
			sahara_debug64(qdl, pkt, ramdump_dir, ramdump_filter);
			break;
		case SAHARA_READ_DATA64_CMD:
			sahara_read64(qdl, pkt, img_arr, single_image);
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

	if (ramdump_dir >= 0)
		close(ramdump_dir);

	return done ? 0 : -1;
}
