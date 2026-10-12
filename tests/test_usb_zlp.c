// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
/*
 * usb_write() terminates a transfer that is a multiple of the max packet
 * size with a zero-length packet only when zlp_on_write is set. Sahara
 * clears the flag, as some implementations time out on the extra packet.
 *
 * usb.c is included directly to reach the private device structure, with
 * libusb_bulk_transfer() replaced by a recording mock.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <cmocka.h>

#define libusb_bulk_transfer mock_libusb_bulk_transfer
#include "../src/usb.c"
#undef libusb_bulk_transfer

bool qdl_debug;

#define MAXPKT		512
#define OUT_EP		0x01
#define MAX_XFERS	8

static int xfer_count;
static int xfer_len[MAX_XFERS];

int mock_libusb_bulk_transfer(struct libusb_device_handle *dev_handle __unused,
			      unsigned char endpoint,
			      unsigned char *data __unused,
			      int length, int *transferred,
			      unsigned int timeout __unused)
{
	assert_int_equal(endpoint, OUT_EP);
	assert_true(xfer_count < MAX_XFERS);

	xfer_len[xfer_count++] = length;
	if (transferred)
		*transferred = length;

	return 0;
}

static int setup(void **state)
{
	struct qdl_device *qdl = usb_init();
	struct qdl_device_usb *qdl_usb;

	if (!qdl)
		return -1;

	qdl_usb = container_of(qdl, struct qdl_device_usb, base);
	qdl_usb->out_ep = OUT_EP;
	qdl_usb->out_maxpktsize = MAXPKT;
	qdl_usb->out_chunk_size = 1024 * 1024;

	xfer_count = 0;
	*state = qdl;

	return 0;
}

static int teardown(void **state)
{
	free(*state);

	return 0;
}

static void test_zlp_default_on(void **state)
{
	struct qdl_device *qdl = *state;

	assert_true(qdl->zlp_on_write);
}

static void test_zlp_sent_when_enabled(void **state)
{
	struct qdl_device *qdl = *state;
	static char buf[2 * MAXPKT];

	assert_int_equal(usb_write(qdl, buf, sizeof(buf), 1000), sizeof(buf));
	assert_int_equal(xfer_count, 2);
	assert_int_equal(xfer_len[0], sizeof(buf));
	assert_int_equal(xfer_len[1], 0);
}

static void test_zlp_skipped_when_disabled(void **state)
{
	struct qdl_device *qdl = *state;
	static char buf[2 * MAXPKT];

	qdl->zlp_on_write = false;

	assert_int_equal(usb_write(qdl, buf, sizeof(buf), 1000), sizeof(buf));
	assert_int_equal(xfer_count, 1);
	assert_int_equal(xfer_len[0], sizeof(buf));
}

static void test_zlp_skipped_when_disabled_chunked(void **state)
{
	struct qdl_device *qdl = *state;
	struct qdl_device_usb *qdl_usb = container_of(qdl, struct qdl_device_usb, base);
	static char buf[4 * MAXPKT];

	qdl_usb->out_chunk_size = 2 * MAXPKT;
	qdl->zlp_on_write = false;

	assert_int_equal(usb_write(qdl, buf, sizeof(buf), 1000), sizeof(buf));
	assert_int_equal(xfer_count, 2);
	assert_int_equal(xfer_len[0], 2 * MAXPKT);
	assert_int_equal(xfer_len[1], 2 * MAXPKT);
}

static void test_no_zlp_for_unaligned_write(void **state)
{
	struct qdl_device *qdl = *state;
	static char buf[MAXPKT + 1];

	assert_int_equal(usb_write(qdl, buf, sizeof(buf), 1000), sizeof(buf));
	assert_int_equal(xfer_count, 1);
	assert_int_equal(xfer_len[0], sizeof(buf));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(test_zlp_default_on, setup, teardown),
		cmocka_unit_test_setup_teardown(test_zlp_sent_when_enabled, setup, teardown),
		cmocka_unit_test_setup_teardown(test_zlp_skipped_when_disabled, setup, teardown),
		cmocka_unit_test_setup_teardown(test_zlp_skipped_when_disabled_chunked, setup, teardown),
		cmocka_unit_test_setup_teardown(test_no_zlp_for_unaligned_write, setup, teardown),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
