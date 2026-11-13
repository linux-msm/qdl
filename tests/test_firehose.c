// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
/*
 * Unit tests for the Firehose XML response parsers, including the drain
 * parser, and the reset mode mapping. They are file-local in firehose.c, so the module is #included
 * directly. Only these helpers are exercised; the rest of firehose.c (and its device / vip /
 * ufs dependencies) is dropped at link time via --gc-sections, so only
 * the ux_* logging stubs are needed.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>
#include <libxml/tree.h>

#include "firehose.c"

void ux_err(const char *fmt, ...) { (void)fmt; }
void ux_log(const char *fmt, ...) { (void)fmt; }
void ux_log_tag(enum ux_tag tag, const char *fmt, ...) { (void)tag; (void)fmt; }

static xmlNode *mk(const char *name)
{
	return xmlNewNode(NULL, (const xmlChar *)name);
}

static void attr(xmlNode *n, const char *k, const char *v)
{
	xmlNewProp(n, (const xmlChar *)k, (const xmlChar *)v);
}

/* --- firehose_generic_parser --- */

static void test_generic_ack_nak_log(void **state)
{
	bool rawmode = false;
	xmlNode *n;
	(void)state;

	n = mk("response");
	attr(n, "value", "ACK");
	assert_int_equal(firehose_generic_parser(n, NULL, &rawmode), FIREHOSE_ACK);
	xmlFreeNode(n);

	n = mk("response");
	attr(n, "value", "NAK");
	assert_int_equal(firehose_generic_parser(n, NULL, &rawmode), FIREHOSE_NAK);
	xmlFreeNode(n);

	n = mk("log");
	attr(n, "value", "some log line");
	assert_int_equal(firehose_generic_parser(n, NULL, &rawmode), -EAGAIN);
	xmlFreeNode(n);
}

static void test_generic_rawmode(void **state)
{
	bool rawmode = false;
	xmlNode *n;
	(void)state;

	n = mk("response");
	attr(n, "value", "ACK");
	attr(n, "rawmode", "true");
	assert_int_equal(firehose_generic_parser(n, NULL, &rawmode), FIREHOSE_ACK);
	assert_true(rawmode);
	xmlFreeNode(n);
}

static void test_generic_missing_value(void **state)
{
	bool rawmode = false;
	xmlNode *n = mk("response");
	(void)state;

	assert_int_equal(firehose_generic_parser(n, NULL, &rawmode), -EINVAL);
	xmlFreeNode(n);
}

/* --- firehose_configure_response_parser --- */

static void test_configure_ack(void **state)
{
	size_t max_size = 0;
	bool rawmode = false;
	xmlNode *n = mk("response");
	(void)state;

	attr(n, "value", "ACK");
	attr(n, "MaxPayloadSizeToTargetInBytes", "1048576");
	attr(n, "MaxPayloadSizeToTargetInBytesSupported", "2097152");

	assert_int_equal(firehose_configure_response_parser(n, &max_size, &rawmode),
			 FIREHOSE_ACK);
	/* An ACK advertises the larger supported size. */
	assert_int_equal(max_size, 2097152);
	xmlFreeNode(n);
}

static void test_configure_nak_negotiates(void **state)
{
	size_t max_size = 0;
	bool rawmode = false;
	xmlNode *n = mk("response");
	(void)state;

	/*
	 * A configure NAK is the payload-size negotiation, not a failure:
	 * the device rejects the host's proposal and answers with the size
	 * it accepts, which the caller re-sends. Treating the NAK as an
	 * error makes the handshake loop forever on such devices.
	 */
	attr(n, "value", "NAK");
	attr(n, "MaxPayloadSizeToTargetInBytes", "1048576");

	assert_int_equal(firehose_configure_response_parser(n, &max_size, &rawmode),
			 FIREHOSE_ACK);
	assert_int_equal(max_size, 1048576);
	xmlFreeNode(n);
}

static void test_configure_ack_without_supported(void **state)
{
	size_t max_size = 0;
	bool rawmode = false;
	xmlNode *n = mk("response");
	(void)state;

	/* An ACK that omits the "Supported" size is malformed. */
	attr(n, "value", "ACK");
	attr(n, "MaxPayloadSizeToTargetInBytes", "1048576");

	assert_true(firehose_configure_response_parser(n, &max_size, &rawmode) < 0);
	xmlFreeNode(n);
}

static void test_configure_log_and_missing(void **state)
{
	size_t max_size = 0;
	bool rawmode = false;
	xmlNode *n;
	(void)state;

	n = mk("log");
	attr(n, "value", "startup log");
	assert_int_equal(firehose_configure_response_parser(n, &max_size, &rawmode), -EAGAIN);
	xmlFreeNode(n);

	n = mk("response");
	assert_int_equal(firehose_configure_response_parser(n, &max_size, &rawmode), -EINVAL);
	xmlFreeNode(n);
}

/* --- firehose_drain_parser --- */

static void test_drain_parser(void **state)
{
	bool rawmode = false;
	xmlNode *n;
	(void)state;

	/* Logs and responses alike end the drain once output pauses. */
	n = mk("log");
	attr(n, "value", "INFO: Binary build date");
	assert_int_equal(firehose_drain_parser(n, NULL, &rawmode), 0);
	xmlFreeNode(n);

	n = mk("response");
	attr(n, "value", "NAK");
	assert_int_equal(firehose_drain_parser(n, NULL, &rawmode), 0);
	xmlFreeNode(n);

	assert_false(rawmode);
}

/* --- firehose_reset_mode_str --- */

static void test_reset_mode_str(void **state)
{
	(void)state;

	assert_string_equal(firehose_reset_mode_str(QDL_RESET_SYSTEM), "reset");
	assert_string_equal(firehose_reset_mode_str(QDL_RESET_EDL), "reset_to_edl");
	assert_string_equal(firehose_reset_mode_str(QDL_RESET_OFF), "off");
	assert_null(firehose_reset_mode_str((enum qdl_reset_mode)-1));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_generic_ack_nak_log),
		cmocka_unit_test(test_generic_rawmode),
		cmocka_unit_test(test_generic_missing_value),
		cmocka_unit_test(test_configure_ack),
		cmocka_unit_test(test_configure_nak_negotiates),
		cmocka_unit_test(test_configure_ack_without_supported),
		cmocka_unit_test(test_configure_log_and_missing),
		cmocka_unit_test(test_drain_parser),
		cmocka_unit_test(test_reset_mode_str),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
