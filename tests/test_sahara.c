// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
/* Unit tests for Sahara session mode selection. */
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "sahara.c"

#define MAX_PKTS 4

struct mock_io {
	struct sahara_pkt reads[MAX_PKTS];
	struct sahara_pkt writes[MAX_PKTS];
	unsigned int read_count;
	unsigned int read_pos;
	unsigned int write_count;
};

static struct mock_io io;

bool qdl_debug;

static void mock_reset(void)
{
	memset(&io, 0, sizeof(io));
}

static void mock_queue_hello(unsigned int mode)
{
	struct sahara_pkt *pkt;

	assert_true(io.read_count < MAX_PKTS);

	pkt = &io.reads[io.read_count++];
	pkt->cmd = SAHARA_HELLO_CMD;
	pkt->length = SAHARA_HELLO_LENGTH;
	pkt->hello_req.version = SAHARA_VERSION;
	pkt->hello_req.compatible = 1;
	pkt->hello_req.max_len = 1024;
	pkt->hello_req.mode = mode;
}

static void mock_queue_done(unsigned int status)
{
	struct sahara_pkt *pkt;

	assert_true(io.read_count < MAX_PKTS);

	pkt = &io.reads[io.read_count++];
	pkt->cmd = SAHARA_DONE_RESP_CMD;
	pkt->length = SAHARA_DONE_RESP_LENGTH;
	pkt->done_resp.status = status;
}

static void mock_queue_reset_resp(void)
{
	struct sahara_pkt *pkt;

	assert_true(io.read_count < MAX_PKTS);

	pkt = &io.reads[io.read_count++];
	pkt->cmd = SAHARA_RESET_RESP_CMD;
	pkt->length = SAHARA_RESET_LENGTH;
}

static void mock_queue_mem_debug64(void)
{
	struct sahara_pkt *pkt;

	assert_true(io.read_count < MAX_PKTS);

	pkt = &io.reads[io.read_count++];
	pkt->cmd = SAHARA_MEM_DEBUG64_CMD;
	pkt->length = SAHARA_MEM_DEBUG64_LENGTH;
}

int qdl_read(struct qdl_device *qdl, void *buf, size_t len,
	     unsigned int timeout)
{
	const struct sahara_pkt *pkt;

	(void)qdl;
	(void)timeout;

	assert_true(io.read_pos < io.read_count);
	pkt = &io.reads[io.read_pos++];
	assert_true(len >= pkt->length);
	memcpy(buf, pkt, pkt->length);

	return pkt->length;
}

int qdl_write(struct qdl_device *qdl, const void *buf, size_t len,
	      unsigned int timeout)
{
	(void)qdl;
	(void)timeout;

	assert_true(io.write_count < MAX_PKTS);
	assert_true(len <= sizeof(io.writes[0]));
	memcpy(&io.writes[io.write_count++], buf, len);

	return len;
}

void ux_err(const char *fmt, ...) { (void)fmt; }
void ux_info(const char *fmt, ...) { (void)fmt; }
void ux_log(const char *fmt, ...) { (void)fmt; }
void ux_debug(const char *fmt, ...) { (void)fmt; }
void ux_warn(const char *fmt, ...) { (void)fmt; }
void ux_progress(const char *fmt, unsigned int value, unsigned int size, ...)
{ (void)fmt; (void)value; (void)size; }
void print_hex_dump(const char *prefix, const void *buf, size_t len)
{ (void)prefix; (void)buf; (void)len; }

static void test_flash_accepts_image_transfer_mode(void **state)
{
	struct sahara_image images[MAPPING_SZ] = {};
	struct qdl_device qdl = {};
	(void)state;

	mock_reset();
	mock_queue_hello(SAHARA_MODE_IMAGE_TX_PENDING);
	mock_queue_done(1);

	assert_int_equal(sahara_run(&qdl, images, NULL, NULL), 0);
	assert_int_equal(io.read_pos, 2);
	assert_int_equal(io.write_count, 1);
	assert_int_equal(io.writes[0].cmd, SAHARA_HELLO_RESP_CMD);
	assert_int_equal(io.writes[0].hello_resp.mode,
			 SAHARA_MODE_IMAGE_TX_PENDING);
}

static void test_flash_rejects_memory_debug_mode(void **state)
{
	struct sahara_image images[MAPPING_SZ] = {};
	struct qdl_device qdl = {};
	(void)state;

	mock_reset();
	mock_queue_hello(SAHARA_MODE_MEMORY_DEBUG);
	mock_queue_mem_debug64();

	assert_true(sahara_run(&qdl, images, NULL, NULL) < 0);
	assert_int_equal(io.read_pos, 1);
	assert_int_equal(io.write_count, 0);
}

static void test_ramdump_accepts_memory_debug_mode(void **state)
{
	struct qdl_device qdl = {};
	(void)state;

	mock_reset();
	mock_queue_hello(SAHARA_MODE_MEMORY_DEBUG);
	mock_queue_reset_resp();

	assert_int_equal(sahara_run(&qdl, NULL, ".", NULL), 0);
	assert_int_equal(io.read_pos, 2);
	assert_int_equal(io.write_count, 1);
	assert_int_equal(io.writes[0].cmd, SAHARA_HELLO_RESP_CMD);
	assert_int_equal(io.writes[0].hello_resp.mode,
			 SAHARA_MODE_MEMORY_DEBUG);
}

static void test_ramdump_rejects_image_transfer_mode(void **state)
{
	struct qdl_device qdl = {};
	(void)state;

	mock_reset();
	mock_queue_hello(SAHARA_MODE_IMAGE_TX_PENDING);
	mock_queue_done(1);

	assert_true(sahara_run(&qdl, NULL, ".", NULL) < 0);
	assert_int_equal(io.read_pos, 1);
	assert_int_equal(io.write_count, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_flash_accepts_image_transfer_mode),
		cmocka_unit_test(test_flash_rejects_memory_debug_mode),
		cmocka_unit_test(test_ramdump_accepts_memory_debug_mode),
		cmocka_unit_test(test_ramdump_rejects_image_transfer_mode),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
