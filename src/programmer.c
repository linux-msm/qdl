// SPDX-License-Identifier: BSD-3-Clause
/*
 * Sahara programmer handling: mapping the user's programmer specifier
 * onto the Sahara image table, and reading and writing the CPIO-based
 * programmer archives that carry multi-image boot chains.
 */
#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qdl.h"
#include "oscompat.h"
#include "programmer.h"

#define CPIO_MAGIC "070701"
struct cpio_newc_header {
	char c_magic[6];       /* "070701" */
	char c_ino[8];
	char c_mode[8];
	char c_uid[8];
	char c_gid[8];
	char c_nlink[8];
	char c_mtime[8];
	char c_filesize[8];
	char c_devmajor[8];
	char c_devminor[8];
	char c_rdevmajor[8];
	char c_rdevminor[8];
	char c_namesize[8];
	char c_check[8];
};

static uint32_t parse_ascii_hex32(const char *s)
{
	uint32_t x = 0;

	for (int i = 0; i < 8; i++) {
		if (!isxdigit(s[i]))
			err(1, "non-hex-digit found in archive header");

		if (s[i] <= '9')
			x = (x << 4) | (s[i] - '0');
		else
			x = (x << 4) | (10 + (s[i] | 32) - 'a');
	}

	return x;
}

/**
 * decode_programmer_archive() - Attempt to decode a programmer CPIO archive
 * @blob: Loaded image to be decoded as archive
 * @images: List of Sahara images to populate
 *
 * The blob might be a CPIO archive containing Sahara images, in files with
 * names in the format "<id>:<filename>". Load each such Sahara image into the
 * relevant spot in the @images array.
 *
 * The blob is always consumed (freed) on both success and error paths.
 * On error, any partially-populated @images entries are also freed.
 *
 * Returns: 0 if no archive was found, 1 if archive was decoded, -1 on error
 */
int decode_programmer_archive(struct sahara_image *blob, struct sahara_image *images)
{
	struct cpio_newc_header *hdr;
	size_t filesize;
	size_t namesize;
	char name[128];
	char *save;
	char *tok;
	void *ptr = blob->ptr;
	void *end = blob->ptr + blob->len;
	long id;

	if (blob->len < sizeof(*hdr) || memcmp(ptr, CPIO_MAGIC, 6))
		return 0;

	for (;;) {
		if (ptr + sizeof(*hdr) > end) {
			ux_err("programmer archive is truncated\n");
			goto err;
		}
		hdr = ptr;

		if (memcmp(hdr->c_magic, "070701", 6)) {
			ux_err("expected cpio header in programmer archive\n");
			goto err;
		}

		filesize = parse_ascii_hex32(hdr->c_filesize);
		namesize = parse_ascii_hex32(hdr->c_namesize);

		ptr += sizeof(*hdr);
		if (ptr + namesize > end || ptr + filesize + namesize > end) {
			ux_err("programmer archive is truncated\n");
			goto err;
		}

		if (namesize == 0 || namesize > sizeof(name)) {
			ux_err("unexpected filename length in programmer archive\n");
			goto err;
		}
		memcpy(name, ptr, namesize);

		if (name[namesize - 1] != '\0') {
			ux_err("malformed filename in programmer archive\n");
			goto err;
		}

		if (!strcmp(name, "TRAILER!!!"))
			break;

		tok = strtok_r(name, ":", &save);
		if (!tok) {
			ux_err("missing image id in programmer archive entry\n");
			goto err;
		}
		id = strtoul(tok, NULL, 0);
		if (id <= 0 || id >= MAPPING_SZ) {
			ux_err("invalid image id \"%s\" in programmer archive\n", tok);
			goto err;
		}

		ptr += namesize;
		ptr = ALIGN_UP(ptr, 4);

		tok = strtok_r(NULL, ":", &save);
		if (tok)
			images[id].name = strdup(tok);
		images[id].len = filesize;
		images[id].ptr = malloc(filesize);
		memcpy(images[id].ptr, ptr, filesize);

		ptr += filesize;
		ptr = ALIGN_UP(ptr, 4);
	}

	free(blob->ptr);
	blob->ptr = NULL;
	blob->len = 0;

	return 1;

err:
	sahara_images_free(images, MAPPING_SZ);
	free(blob->ptr);
	blob->ptr = NULL;
	blob->len = 0;
	return -1;
}

/**
 * decode_programmer() - decodes the programmer specifier
 * @s: programmer specifier, from the user
 * @images: array of images to populate
 *
 * This parses the programmer specifier @s, which can either be a single
 * filename, or a comma-separated series of <id>:<filename> entries.
 *
 * In the first case an attempt will be made to decode the Sahara archive and
 * each programmer part will be loaded into their requested @images entry. If
 * the file isn't an archive @images[SAHARA_ID_EHOSTDL_IMG] is assigned. In the
 * second case, each comma-separated entry will be split on ':' and the given
 * <filename> will be assigned to the @image entry indicated by the given <id>.
 *
 * Memory is not allocated for the various strings, instead @s will be modified
 * by the tokenizer and pointers to the individual parts will be stored in the
 * @images array.
 *
 * Returns: 0 on success, -1 otherwise.
 */
int decode_programmer(char *s, struct sahara_image *images)
{
	struct sahara_image archive;
	char *filename;
	char *save1;
	char *pair;
	char *tail;
	long id;
	int ret;

	strtoul(s, &tail, 0);
	if (tail != s && tail[0] == ':') {
		for (pair = strtok_r(s, ",", &save1); pair; pair = strtok_r(NULL, ",", &save1)) {
			id = strtoul(pair, &tail, 0);
			if (tail == pair) {
				ux_err("invalid programmer specifier\n");
				return -1;
			}

			if (id <= 0 || id >= MAPPING_SZ) {
				ux_err("invalid image id \"%s\"\n", pair);
				return -1;
			}

			filename = &tail[1];
			ret = load_sahara_image(NULL, filename, &images[id]);
			if (ret < 0)
				return -1;
		}
	} else {
		ret = load_sahara_image(NULL, s, &archive);
		if (ret < 0)
			return -1;

		ret = decode_programmer_archive(&archive, images);
		if (ret < 0 || ret == 1)
			return ret;

		ret = decode_sahara_config(&archive, images, NULL);
		if (ret < 0 || ret == 1)
			return ret;

		images[SAHARA_ID_EHOSTDL_IMG] = archive;
	}

	return 0;
}

static char *qdl_basename_dup(const char *path)
{
	char *tmp;
	char *base;

	tmp = strdup(path);
	if (!tmp)
		return NULL;

	base = basename(tmp);
	base = base ? strdup(base) : NULL;
	free(tmp);

	return base;
}

static int sahara_archive_pad(FILE *out, size_t len)
{
	static const char zeros[4];
	size_t pad = ROUND_UP(len, 4) - len;

	if (!pad)
		return 0;

	return fwrite(zeros, 1, pad, out) == pad ? 0 : -1;
}

static int sahara_archive_write_entry(FILE *out, unsigned int ino, const char *name,
				      const void *data, size_t len)
{
	char header[sizeof(struct cpio_newc_header) + 1];
	size_t namesize = strlen(name) + 1;

	if (len > UINT32_MAX || namesize > UINT32_MAX) {
		ux_err("sahara archive entry \"%s\" is too large\n", name);
		return -1;
	}

	snprintf(header, sizeof(header),
		 "%s%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
		 CPIO_MAGIC, ino, 0100644, 0, 0, 1, 0, (unsigned int)len,
		 0, 0, 0, 0, (unsigned int)namesize, 0);

	if (fwrite(header, 1, sizeof(struct cpio_newc_header), out) !=
	    sizeof(struct cpio_newc_header))
		return -1;
	if (fwrite(name, 1, namesize, out) != namesize)
		return -1;
	if (sahara_archive_pad(out, sizeof(struct cpio_newc_header) + namesize) < 0)
		return -1;
	if (len && fwrite(data, 1, len, out) != len)
		return -1;
	if (sahara_archive_pad(out, len) < 0)
		return -1;

	return 0;
}

int sahara_archive_write(const char *filename, const struct sahara_image *images)
{
	char *basename = NULL;
	char name[128];
	unsigned int ino = 1;
	unsigned int count = 0;
	FILE *out;
	int ret = -1;
	int i;

	for (i = 1; i < MAPPING_SZ; i++) {
		if (images[i].ptr)
			count++;
	}

	if (!count) {
		ux_err("no Sahara images found for archive\n");
		return -1;
	}

	out = fopen(filename, "wb");
	if (!out) {
		ux_err("failed to create \"%s\": %s\n", filename, strerror(errno));
		return -1;
	}

	for (i = 1; i < MAPPING_SZ; i++) {
		if (!images[i].ptr)
			continue;

		free(basename);
		basename = images[i].name ? qdl_basename_dup(images[i].name) : NULL;
		if (basename)
			snprintf(name, sizeof(name), "%d:%s", i, basename);
		else
			snprintf(name, sizeof(name), "%d", i);

		if (sahara_archive_write_entry(out, ino++, name, images[i].ptr,
					       images[i].len) < 0)
			goto out_close;
	}

	ret = sahara_archive_write_entry(out, ino++, "TRAILER!!!", NULL, 0);

out_close:
	free(basename);
	if (fclose(out) && ret == 0)
		ret = -1;
	if (ret < 0) {
		ux_err("failed to write Sahara archive \"%s\"\n", filename);
		remove(filename);
	}

	return ret;
}
