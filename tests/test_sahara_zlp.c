// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
/*
 * Sahara transfers must not be terminated with a zero-length packet, as
 * some implementations time out the endpoint on it. Drive sahara_run(),
 * sahara_chipinfo() and sahara_device_reset() against a scripted device and
 * check that zlp_on_write is clear for every write and restored afterwards.
 */
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "qdl.h"

#define IMAGE_LEN	1024

#define SAHARA_HELLO_CMD		0x1
#define SAHARA_READ_DATA_CMD		0x3
#define SAHARA_END_OF_IMAGE_CMD		0x4
#define SAHARA_DONE_RESP_CMD		0x6
#define SAHARA_RESET_RESP_CMD		0x8

#define SAHARA_MODE_MEMORY_DEBUG	0x2

#define MAX_WRITES	8

struct pkt {
	uint32_t words[12];
	size_t len;
};

bool qdl_debug;

static const struct pkt *script;
static size_t script_len;
static size_t script_pos;
static int script_end_err;

static int write_count;
static bool write_zlp[MAX_WRITES];
static size_t write_len[MAX_WRITES];

int qdl_read(struct qdl_device *qdl __unused, void *buf, size_t len,
	     unsigned int timeout __unused)
{
	const struct pkt *p;

	if (script_pos >= script_len)
		return script_end_err;

	p = &script[script_pos++];
	assert_true(p->len <= len);
	memcpy(buf, p->words, p->len);

	return p->len;
}

int qdl_write(struct qdl_device *qdl, const void *buf __unused, size_t len,
	      unsigned int timeout __unused)
{
	assert_true(write_count < MAX_WRITES);
	write_zlp[write_count] = qdl->zlp_on_write;
	write_len[write_count++] = len;

	return len;
}

static const struct pkt full_session[] = {
	{ { SAHARA_HELLO_CMD, 0x30, 2, 1, 1024, 0 }, 0x30 },
	{ { SAHARA_READ_DATA_CMD, 0x14, 0, 0, IMAGE_LEN }, 0x14 },
	{ { SAHARA_END_OF_IMAGE_CMD, 0x10, 0, 0 }, 0x10 },
	{ { SAHARA_DONE_RESP_CMD, 0xc, 1 }, 0xc },
};

/*
 * Crash-mode device: HELLO announces memory debug, which the reset helper
 * answers in kind and then sends the reset request straight away.
 */
static const struct pkt reset_session[] = {
	{ { SAHARA_HELLO_CMD, 0x30, 2, 1, 1024, SAHARA_MODE_MEMORY_DEBUG }, 0x30 },
	{ { SAHARA_RESET_RESP_CMD, 0x8 }, 0x8 },
};

static const struct pkt reset_rejected[] = {
	{ { SAHARA_HELLO_CMD, 0x30, 2, 1, 1024, SAHARA_MODE_MEMORY_DEBUG }, 0x30 },
	{ { SAHARA_END_OF_IMAGE_CMD, 0x10, 0, 1 }, 0x10 },
};

/* The device refuses command mode right after the HELLO response */
static const struct pkt chipinfo_rejected[] = {
	{ { SAHARA_HELLO_CMD, 0x30, 2, 1, 1024, 0 }, 0x30 },
	{ { SAHARA_END_OF_IMAGE_CMD, 0x10, 0, 1 }, 0x10 },
};

static int setup(void **state)
{
	static struct qdl_device qdl;

	memset(&qdl, 0, sizeof(qdl));
	qdl.dev_type = QDL_DEVICE_USB;
	qdl.zlp_on_write = true;

	script = NULL;
	script_len = 0;
	script_pos = 0;
	script_end_err = -EIO;
	write_count = 0;

	*state = &qdl;

	return 0;
}

static void test_run_writes_without_zlp(void **state)
{
	struct qdl_device *qdl = *state;
	static char data[IMAGE_LEN];
	struct sahara_image images[16] = {};
	int i;

	images[0].name = "prog";
	images[0].ptr = data;
	images[0].len = sizeof(data);

	script = full_session;
	script_len = ARRAY_SIZE(full_session);

	assert_int_equal(sahara_run(qdl, images, NULL, NULL), 0);

	/* HELLO response, image data, DONE */
	assert_int_equal(write_count, 3);
	assert_int_equal(write_len[1], IMAGE_LEN);
	for (i = 0; i < write_count; i++)
		assert_false(write_zlp[i]);

	assert_true(qdl->zlp_on_write);
}

static void test_run_restores_zlp_on_failure(void **state)
{
	struct qdl_device *qdl = *state;

	/* No packets at all: the first read fails */
	assert_int_not_equal(sahara_run(qdl, NULL, NULL, NULL), 0);

	assert_true(qdl->zlp_on_write);
}

static void test_device_reset_writes_without_zlp(void **state)
{
	struct qdl_device *qdl = *state;
	int i;

	script = reset_session;
	script_len = ARRAY_SIZE(reset_session);

	/*
	 * The script ends after the reset response, so the follow-up read
	 * fails with a transport error: the device is gone, reset done.
	 */
	assert_int_equal(sahara_device_reset(qdl), 0);

	/* HELLO response, reset request */
	assert_int_equal(write_count, 2);
	for (i = 0; i < write_count; i++)
		assert_false(write_zlp[i]);

	assert_true(qdl->zlp_on_write);
}

static void test_device_reset_restores_zlp_on_failure(void **state)
{
	struct qdl_device *qdl = *state;
	int i;

	script = reset_rejected;
	script_len = ARRAY_SIZE(reset_rejected);

	assert_int_equal(sahara_device_reset(qdl), -1);

	for (i = 0; i < write_count; i++)
		assert_false(write_zlp[i]);

	assert_true(qdl->zlp_on_write);
}

static void test_chipinfo_writes_without_zlp(void **state)
{
	struct qdl_device *qdl = *state;
	int i;

	script = chipinfo_rejected;
	script_len = ARRAY_SIZE(chipinfo_rejected);

	assert_int_equal(sahara_chipinfo(qdl), -1);

	/* HELLO response requesting command mode */
	assert_int_equal(write_count, 1);
	for (i = 0; i < write_count; i++)
		assert_false(write_zlp[i]);

	assert_true(qdl->zlp_on_write);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_run_writes_without_zlp, setup),
		cmocka_unit_test_setup(test_run_restores_zlp_on_failure, setup),
		cmocka_unit_test_setup(test_device_reset_writes_without_zlp, setup),
		cmocka_unit_test_setup(test_device_reset_restores_zlp_on_failure, setup),
		cmocka_unit_test_setup(test_chipinfo_writes_without_zlp, setup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
