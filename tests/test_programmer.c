// SPDX-License-Identifier: BSD-3-Clause
/*
 * Unit tests for the programmer CPIO archive decoder (programmer.c). The
 * decoder parses an untrusted blob, so the tests craft small newc CPIO
 * archives and verify both correct decoding and rejection of the
 * malformed inputs the parser was hardened against.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "qdl.h"
#include "programmer.h"

/* --- stubs --- */
void ux_err(const char *fmt, ...) { (void)fmt; }

/*
 * decode_programmer() references the Sahara image loader; the tests
 * exercise only the archive decoder, so a failing stub satisfies the
 * linker without pulling in sahara.c.
 */
int load_sahara_image(struct qdl_zip *zip, const char *filename, struct sahara_image *image)
{
	(void)zip;
	(void)filename;
	(void)image;
	return -1;
}

int decode_sahara_config(struct sahara_image *blob, struct sahara_image *images,
			 struct contents_filter *filter)
{
	(void)blob;
	(void)images;
	(void)filter;
	return 0;
}

#ifdef _WIN32
/* err() lives in oscompat.c, which the test does not link. */
void err(int eval, const char *fmt, ...)
{
	(void)fmt;
	exit(eval);
}
#endif

void sahara_images_free(struct sahara_image *images, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		free(images[i].name);
		free(images[i].ptr);
		images[i] = (struct sahara_image){0};
	}
}

/* --- newc CPIO archive builder --- */
#define CPIO_HDR_LEN	110
#define OFF_FILESIZE	54
#define OFF_NAMESIZE	94

struct builder {
	uint8_t *buf;
	size_t len;
	size_t cap;
};

static void bput(struct builder *b, const void *data, size_t n)
{
	if (b->len + n > b->cap) {
		b->cap = (b->len + n) * 2 + 64;
		b->buf = realloc(b->buf, b->cap);
		assert_non_null(b->buf);
	}
	memcpy(b->buf + b->len, data, n);
	b->len += n;
}

static void bpad4(struct builder *b)
{
	static const uint8_t zero[4] = {0};

	if (b->len % 4)
		bput(b, zero, 4 - (b->len % 4));
}

static void put_hexfield(uint8_t *hdr, size_t off, uint32_t value)
{
	char tmp[9];

	snprintf(tmp, sizeof(tmp), "%08x", value);
	memcpy(hdr + off, tmp, 8);
}

/*
 * Append one entry. @name is written verbatim as the CPIO name of
 * @name_size bytes (so a caller can produce an unterminated name); pass
 * name_size == 0 to use strlen(name) + 1.
 */
static void add_entry(struct builder *b, const char *name, size_t name_size,
		      const void *data, size_t data_len)
{
	uint8_t hdr[CPIO_HDR_LEN];

	if (name_size == 0)
		name_size = strlen(name) + 1;

	memset(hdr, '0', sizeof(hdr));
	memcpy(hdr, "070701", 6);
	put_hexfield(hdr, OFF_FILESIZE, (uint32_t)data_len);
	put_hexfield(hdr, OFF_NAMESIZE, (uint32_t)name_size);

	bput(b, hdr, sizeof(hdr));
	bput(b, name, name_size);
	bpad4(b);
	if (data_len)
		bput(b, data, data_len);
	bpad4(b);
}

static void add_trailer(struct builder *b)
{
	add_entry(b, "TRAILER!!!", 0, NULL, 0);
}

/* Move the built archive into a heap blob the decoder can consume. */
static void into_blob(struct builder *b, struct sahara_image *blob)
{
	blob->ptr = malloc(b->len);
	assert_non_null(blob->ptr);
	memcpy(blob->ptr, b->buf, b->len);
	blob->len = b->len;
	blob->name = NULL;
	free(b->buf);
	b->buf = NULL;
}

static struct sahara_image *zero_images(void)
{
	static struct sahara_image images[MAPPING_SZ];

	memset(images, 0, sizeof(images));
	return images;
}

/* --- tests --- */

static void test_valid_archive(void **state)
{
	struct builder b = {0};
	struct sahara_image blob = {0};
	struct sahara_image *images = zero_images();
	(void)state;

	add_entry(&b, "1:prog", 0, "DATA", 4);
	add_trailer(&b);
	into_blob(&b, &blob);

	assert_int_equal(decode_programmer_archive(&blob, images), 1);
	assert_non_null(images[1].name);
	assert_string_equal(images[1].name, "prog");
	assert_int_equal(images[1].len, 4);
	assert_memory_equal(images[1].ptr, "DATA", 4);
	/* The blob is consumed on success. */
	assert_null(blob.ptr);

	sahara_images_free(images, MAPPING_SZ);
	free(blob.ptr);
}

static void test_not_an_archive(void **state)
{
	struct sahara_image blob;
	struct sahara_image *images = zero_images();
	(void)state;

	blob.ptr = strdup("not a cpio archive, definitely longer than a header ..............");
	blob.len = strlen(blob.ptr);
	blob.name = NULL;

	/* No CPIO magic -> not an archive; blob is left for the caller. */
	assert_int_equal(decode_programmer_archive(&blob, images), 0);
	free(blob.ptr);
}

static void test_reject_bad_image_id(void **state)
{
	struct builder b = {0};
	struct sahara_image blob = {0};
	struct sahara_image *images = zero_images();
	(void)state;

	/* An id that overflows a long parses negative -> must be rejected. */
	add_entry(&b, "0xffffffffffffffff:prog", 0, "DATA", 4);
	add_trailer(&b);
	into_blob(&b, &blob);

	assert_int_equal(decode_programmer_archive(&blob, images), -1);
	sahara_images_free(images, MAPPING_SZ);
}

static void test_reject_zero_image_id(void **state)
{
	struct builder b = {0};
	struct sahara_image blob = {0};
	struct sahara_image *images = zero_images();
	(void)state;

	add_entry(&b, "0:prog", 0, "DATA", 4);
	add_trailer(&b);
	into_blob(&b, &blob);

	assert_int_equal(decode_programmer_archive(&blob, images), -1);
	sahara_images_free(images, MAPPING_SZ);
}

static void test_reject_unterminated_name(void **state)
{
	struct builder b = {0};
	struct sahara_image blob = {0};
	struct sahara_image *images = zero_images();
	(void)state;

	/* Name field with no NUL within its declared size. */
	add_entry(&b, "1:prog", 6, "DATA", 4);
	add_trailer(&b);
	into_blob(&b, &blob);

	assert_int_equal(decode_programmer_archive(&blob, images), -1);
	sahara_images_free(images, MAPPING_SZ);
}

static void test_reject_truncated(void **state)
{
	struct builder b = {0};
	struct sahara_image blob = {0};
	struct sahara_image *images = zero_images();
	(void)state;

	add_entry(&b, "1:prog", 0, "DATA", 4);
	into_blob(&b, &blob);
	/* Chop the archive so the declared data runs past the end. */
	blob.len -= 4;

	assert_int_equal(decode_programmer_archive(&blob, images), -1);
	sahara_images_free(images, MAPPING_SZ);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_valid_archive),
		cmocka_unit_test(test_not_an_archive),
		cmocka_unit_test(test_reject_bad_image_id),
		cmocka_unit_test(test_reject_zero_image_id),
		cmocka_unit_test(test_reject_unterminated_name),
		cmocka_unit_test(test_reject_truncated),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
