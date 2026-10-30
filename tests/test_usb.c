// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
/* Unit tests for USB bulk-IN ZLP handling and serial discovery. */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>
#include <libusb.h>

struct mock_transfer {
	int ret;
	int actual;
	const char *data;
};

static struct mock_transfer transfers[4];
static size_t transfer_count;
static size_t transfer_index;

int libusb_bulk_transfer(libusb_device_handle *dev_handle,
			 unsigned char endpoint, unsigned char *data,
			 int length, int *transferred, unsigned int timeout)
{
	struct mock_transfer *transfer;

	(void)dev_handle;
	(void)endpoint;
	(void)timeout;
	assert_true(transfer_index < transfer_count);
	transfer = &transfers[transfer_index++];
	assert_true(transfer->actual <= length);
	if (transfer->data)
		memcpy(data, transfer->data, transfer->actual);
	*transferred = transfer->actual;

	return transfer->ret;
}

struct mock_string {
	uint8_t index;
	int ret;
	const char *str;
};

static struct mock_string strings[8];
static size_t string_count;
static size_t string_index;

int libusb_get_string_descriptor_ascii(libusb_device_handle *dev_handle,
				       uint8_t desc_index, unsigned char *data,
				       int length)
{
	struct mock_string *string;
	size_t len;

	(void)dev_handle;
	assert_true(string_index < string_count);
	string = &strings[string_index++];
	assert_int_equal(desc_index, string->index);
	if (string->ret < 0)
		return string->ret;

	len = strlen(string->str);
	assert_true(len < (size_t)length);
	memcpy(data, string->str, len + 1);

	return (int)len;
}

void ux_warnx(const char *fmt, ...)
{
	(void)fmt;
}

#include "usb.c"

static void set_strings(const struct mock_string *values, size_t count)
{
	assert_true(count <= ARRAY_SIZE(strings));
	memcpy(strings, values, count * sizeof(*values));
	string_count = count;
	string_index = 0;
}

static void set_transfers(const struct mock_transfer *values, size_t count)
{
	assert_true(count <= ARRAY_SIZE(transfers));
	memcpy(transfers, values, count * sizeof(*values));
	transfer_count = count;
	transfer_index = 0;
}

static void init_usb(struct qdl_device_usb *usb)
{
	memset(usb, 0, sizeof(*usb));
	usb->in_ep = 0x81;
	usb->in_maxpktsize = 512;
}

static void test_zlp_consumed_by_next_read(void **state)
{
	const struct mock_transfer first[] = {
		{ 0, 512, NULL },
	};
	const struct mock_transfer second[] = {
		{ 0, 0, NULL },
		{ 0, 3, "ACK" },
	};
	struct qdl_device_usb usb;
	char buf[512];

	(void)state;
	init_usb(&usb);
	set_transfers(first, ARRAY_SIZE(first));
	assert_int_equal(usb_read(&usb.base, buf, sizeof(buf), 1000), 512);
	assert_true(usb.expect_zlp);
	assert_int_equal(transfer_index, 1);

	set_transfers(second, ARRAY_SIZE(second));
	assert_int_equal(usb_read(&usb.base, buf, sizeof(buf), 1000), 3);
	assert_memory_equal(buf, "ACK", 3);
	assert_false(usb.expect_zlp);
	assert_int_equal(transfer_index, 2);
}

static void test_omitted_zlp_returns_data(void **state)
{
	const struct mock_transfer transfer[] = {
		{ 0, 3, "ACK" },
	};
	struct qdl_device_usb usb;
	char buf[512];

	(void)state;
	init_usb(&usb);
	usb.expect_zlp = true;
	set_transfers(transfer, ARRAY_SIZE(transfer));
	assert_int_equal(usb_read(&usb.base, buf, sizeof(buf), 1000), 3);
	assert_memory_equal(buf, "ACK", 3);
	assert_false(usb.expect_zlp);
	assert_int_equal(transfer_index, 1);
}

static void test_zlp_timeout_preserves_state(void **state)
{
	const struct mock_transfer timeout[] = {
		{ LIBUSB_ERROR_TIMEOUT, 0, NULL },
	};
	const struct mock_transfer retry[] = {
		{ 0, 0, NULL },
		{ 0, 3, "ACK" },
	};
	struct qdl_device_usb usb;
	char buf[512];

	(void)state;
	init_usb(&usb);
	usb.expect_zlp = true;
	set_transfers(timeout, ARRAY_SIZE(timeout));
	assert_int_equal(usb_read(&usb.base, buf, sizeof(buf), 1000),
			 -ETIMEDOUT);
	assert_true(usb.expect_zlp);

	set_transfers(retry, ARRAY_SIZE(retry));
	assert_int_equal(usb_read(&usb.base, buf, sizeof(buf), 1000), 3);
	assert_false(usb.expect_zlp);
}

static void test_partial_timeout_returns_data(void **state)
{
	const struct mock_transfer transfer[] = {
		{ LIBUSB_ERROR_TIMEOUT, 3, "ACK" },
	};
	struct qdl_device_usb usb;
	char buf[512];

	(void)state;
	init_usb(&usb);
	set_transfers(transfer, ARRAY_SIZE(transfer));
	assert_int_equal(usb_read(&usb.base, buf, sizeof(buf), 1000), 3);
	assert_memory_equal(buf, "ACK", 3);
}

static void test_error_clears_zlp_state(void **state)
{
	const struct mock_transfer no_device[] = {
		{ LIBUSB_ERROR_NO_DEVICE, 0, NULL },
	};
	const struct mock_transfer io_error[] = {
		{ LIBUSB_ERROR_IO, 0, NULL },
	};
	struct qdl_device_usb usb;
	char buf[512];

	(void)state;
	init_usb(&usb);
	usb.expect_zlp = true;
	set_transfers(no_device, ARRAY_SIZE(no_device));
	assert_int_equal(usb_read(&usb.base, buf, sizeof(buf), 1000), -ENODEV);
	assert_false(usb.expect_zlp);

	usb.expect_zlp = true;
	set_transfers(io_error, ARRAY_SIZE(io_error));
	assert_int_equal(usb_read(&usb.base, buf, sizeof(buf), 1000), -EIO);
	assert_false(usb.expect_zlp);
}

static const struct libusb_device_descriptor edl_desc = {
	.iProduct = 2,
	.iSerialNumber = 0,
};

static const struct libusb_device_descriptor android_desc = {
	.iProduct = 2,
	.iSerialNumber = 3,
};

static void test_serial_from_iproduct_token(void **state)
{
	const struct mock_string edl[] = {
		{ 2, 0, "QUSB_BULK_CID:0402_SN:9DBBF967" },
	};
	char serial[16];

	(void)state;
	set_strings(edl, ARRAY_SIZE(edl));
	assert_true(usb_read_serial(NULL, &edl_desc, serial, sizeof(serial)));
	assert_string_equal(serial, "9DBBF967");
	assert_int_equal(string_index, 1);
}

static void test_serial_falls_back_to_iserial(void **state)
{
	const struct mock_string android[] = {
		{ 2, 0, "qcs6490-rb3gen2-vision-kit" },
		{ 3, 0, "9dbbf967" },
	};
	char serial[16];

	(void)state;
	set_strings(android, ARRAY_SIZE(android));
	assert_true(usb_read_serial(NULL, &android_desc, serial, sizeof(serial)));
	assert_string_equal(serial, "9dbbf967");
	assert_int_equal(string_index, 2);
}

static void test_serial_prefers_iproduct_token(void **state)
{
	const struct mock_string both[] = {
		{ 2, 0, "QUSB_BULK_CID:0402_SN:9DBBF967" },
	};
	char serial[16];

	(void)state;
	set_strings(both, ARRAY_SIZE(both));
	assert_true(usb_read_serial(NULL, &android_desc, serial, sizeof(serial)));
	assert_string_equal(serial, "9DBBF967");
	assert_int_equal(string_index, 1);
}

static void test_serial_missing_without_iserial(void **state)
{
	const struct mock_string plain[] = {
		{ 2, 0, "qcs6490-rb3gen2-vision-kit" },
	};
	char serial[16];

	(void)state;
	set_strings(plain, ARRAY_SIZE(plain));
	assert_false(usb_read_serial(NULL, &edl_desc, serial, sizeof(serial)));
	assert_int_equal(string_index, 1);
}

static void test_serial_rejects_empty_iserial(void **state)
{
	const struct mock_string empty[] = {
		{ 2, 0, "qcs6490-rb3gen2-vision-kit" },
		{ 3, 0, "" },
	};
	char serial[16];

	(void)state;
	set_strings(empty, ARRAY_SIZE(empty));
	assert_false(usb_read_serial(NULL, &android_desc, serial, sizeof(serial)));
	assert_int_equal(string_index, 2);
}

static void test_serial_retries_transient_error(void **state)
{
	const struct mock_string flaky[] = {
		{ 2, LIBUSB_ERROR_IO, NULL },
		{ 2, LIBUSB_ERROR_PIPE, NULL },
		{ 2, 0, "QUSB_BULK_CID:0402_SN:9DBBF967" },
	};
	char serial[16];

	(void)state;
	set_strings(flaky, ARRAY_SIZE(flaky));
	assert_true(usb_read_serial(NULL, &edl_desc, serial, sizeof(serial)));
	assert_string_equal(serial, "9DBBF967");
	assert_int_equal(string_index, 3);
}

static void test_serial_gives_up_after_retries(void **state)
{
	const struct mock_string wedged[] = {
		{ 2, LIBUSB_ERROR_IO, NULL },
		{ 2, LIBUSB_ERROR_IO, NULL },
		{ 2, LIBUSB_ERROR_IO, NULL },
	};
	char serial[16];

	(void)state;
	set_strings(wedged, ARRAY_SIZE(wedged));
	assert_false(usb_read_serial(NULL, &edl_desc, serial, sizeof(serial)));
	assert_int_equal(string_index, USB_STRING_ATTEMPTS);
}

static void test_serial_no_retry_on_fatal_error(void **state)
{
	const struct mock_string gone[] = {
		{ 2, LIBUSB_ERROR_NO_DEVICE, NULL },
	};
	char serial[16];

	(void)state;
	set_strings(gone, ARRAY_SIZE(gone));
	assert_false(usb_read_serial(NULL, &edl_desc, serial, sizeof(serial)));
	assert_int_equal(string_index, 1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_serial_from_iproduct_token),
		cmocka_unit_test(test_serial_falls_back_to_iserial),
		cmocka_unit_test(test_serial_prefers_iproduct_token),
		cmocka_unit_test(test_serial_missing_without_iserial),
		cmocka_unit_test(test_serial_rejects_empty_iserial),
		cmocka_unit_test(test_serial_retries_transient_error),
		cmocka_unit_test(test_serial_gives_up_after_retries),
		cmocka_unit_test(test_serial_no_retry_on_fatal_error),
		cmocka_unit_test(test_zlp_consumed_by_next_read),
		cmocka_unit_test(test_omitted_zlp_returns_data),
		cmocka_unit_test(test_zlp_timeout_preserves_state),
		cmocka_unit_test(test_partial_timeout_returns_data),
		cmocka_unit_test(test_error_clears_zlp_state),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
