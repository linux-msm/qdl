// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * All rights reserved.
 */
#include <sys/types.h>
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

/* Minimal ELF64 definitions — <elf.h> is not available on all platforms */
#define ELFMAG		"\177ELF"
#define SELFMAG		4
#define ET_CORE		4
#define PT_LOAD		1

#define EI_NIDENT	16
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint64_t Elf64_Xword;

typedef struct {
	unsigned char	e_ident[EI_NIDENT];
	Elf64_Half	e_type;
	Elf64_Half	e_machine;
	Elf64_Word	e_version;
	Elf64_Addr	e_entry;
	Elf64_Off	e_phoff;
	Elf64_Off	e_shoff;
	Elf64_Word	e_flags;
	Elf64_Half	e_ehsize;
	Elf64_Half	e_phentsize;
	Elf64_Half	e_phnum;
	Elf64_Half	e_shentsize;
	Elf64_Half	e_shnum;
	Elf64_Half	e_shstrndx;
} Elf64_Ehdr;

typedef struct {
	Elf64_Word	p_type;
	Elf64_Word	p_flags;
	Elf64_Off	p_offset;
	Elf64_Addr	p_vaddr;
	Elf64_Addr	p_paddr;
	Elf64_Xword	p_filesz;
	Elf64_Xword	p_memsz;
	Elf64_Xword	p_align;
} Elf64_Phdr;

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
#define SAHARA_EXECUTE_LENGTH		0xc
#define SAHARA_SWITCH_MODE_LENGTH	0xc

/* Sahara command-mode client commands, carried by SAHARA_EXECUTE_CMD */
#define SAHARA_EXEC_CMD_SERIAL_NUM_READ		0x01
#define SAHARA_EXEC_CMD_MSM_HW_ID_READ		0x02
#define SAHARA_EXEC_CMD_OEM_PK_HASH_READ	0x03
#define SAHARA_EXEC_CMD_READ_CHIP_ID_V3		0x0a

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
		struct {
			uint32_t client_cmd;
		} exec_req;
		struct {
			uint32_t client_cmd;
			uint32_t data_length;
		} exec_resp;
		struct {
			uint32_t mode;
		} switch_mode;
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

static int sahara_send_hello_resp(struct qdl_device *qdl, unsigned int version,
				  unsigned int mode)
{
	struct sahara_pkt resp = {};

	resp.cmd = SAHARA_HELLO_RESP_CMD;
	resp.length = SAHARA_HELLO_LENGTH;
	resp.hello_resp.version = version;
	resp.hello_resp.compatible = 1;
	resp.hello_resp.status = SAHARA_SUCCESS;
	resp.hello_resp.mode = mode;

	qdl_write(qdl, &resp, resp.length, SAHARA_CMD_TIMEOUT_MS);
	return 0;
}

static int sahara_hello(struct qdl_device *qdl, struct sahara_pkt *pkt)
{
	if (pkt->length != SAHARA_HELLO_LENGTH) {
		ux_err("unexpected HELLO packet length %u\n", pkt->length);
		sahara_send_reset(qdl);
		return -1;
	}

	ux_debug("HELLO version: 0x%x compatible: 0x%x max_len: %d mode: %d\n",
		 pkt->hello_req.version, pkt->hello_req.compatible, pkt->hello_req.max_len, pkt->hello_req.mode);

	return sahara_send_hello_resp(qdl, SAHARA_VERSION, pkt->hello_req.mode);
}

static int sahara_read(struct qdl_device *qdl, struct sahara_pkt *pkt,
		       const struct sahara_image *images)
{
	const struct sahara_image *image;
	unsigned int image_idx;
	size_t offset;
	size_t len;
	int ret;

	if (pkt->length != SAHARA_READ_DATA_LENGTH) {
		ux_err("unexpected READ_DATA packet length %u\n", pkt->length);
		sahara_send_reset(qdl);
		return -1;
	}

	ux_debug("READ image: %d offset: 0x%x length: 0x%x\n",
		 pkt->read_req.image, pkt->read_req.offset, pkt->read_req.length);

	image_idx = pkt->read_req.image;
	if (image_idx >= MAPPING_SZ || !images[image_idx].ptr) {
		ux_err("device requested unknown image id %u, ensure that all Sahara images are provided\n",
		       image_idx);
		sahara_send_reset(qdl);
		return -1;
	}

	offset = pkt->read_req.offset;
	len = pkt->read_req.length;

	image = &images[image_idx];
	if (offset > image->len || offset + len > image->len) {
		ux_err("device requested invalid range of image %d\n", image_idx);
		return -1;
	}

	if (offset == 0)
		ux_info("Sahara: sending %s (%zu bytes)\n",
			image->name ? image->name : "(unknown)", image->len);
	ux_progress("%s", offset + len, image->len, image->name ? image->name : "image");

	ret = qdl_write(qdl, image->ptr + offset, len, SAHARA_CMD_TIMEOUT_MS);
	if (ret < 0 || ((size_t)ret != len)) {
		ux_err("failed to write %zu bytes to sahara\n", len);
		return -1;
	}

	return 0;
}

static int sahara_read64(struct qdl_device *qdl, struct sahara_pkt *pkt,
			 const struct sahara_image *images)
{
	const struct sahara_image *image;
	unsigned int image_idx;
	size_t offset;
	size_t len;
	int ret;

	if (pkt->length != SAHARA_READ_DATA64_LENGTH) {
		ux_err("unexpected READ_DATA64 packet length %u\n", pkt->length);
		sahara_send_reset(qdl);
		return -1;
	}

	ux_debug("READ64 image: %" PRId64 " offset: 0x%" PRIx64 " length: 0x%" PRIx64 "\n",
		 pkt->read64_req.image, pkt->read64_req.offset, pkt->read64_req.length);

	image_idx = pkt->read64_req.image;
	if (image_idx >= MAPPING_SZ || !images[image_idx].ptr) {
		ux_err("device requested unknown image id %u, ensure that all Sahara images are provided\n",
		       image_idx);
		sahara_send_reset(qdl);
		return -1;
	}

	offset = pkt->read64_req.offset;
	len = pkt->read64_req.length;

	image = &images[image_idx];
	if (offset > image->len || offset + len > image->len) {
		ux_err("device requested invalid range of image %d\n", image_idx);
		return -1;
	}

	if (offset == 0)
		ux_info("Sahara: sending %s (%zu bytes)\n",
			image->name ? image->name : "(unknown)", image->len);
	ux_progress("%s", offset + len, image->len, image->name ? image->name : "image");

	ret = qdl_write(qdl, image->ptr + offset, len, SAHARA_CMD_TIMEOUT_MS);
	if (ret < 0 || ((size_t)ret != len)) {
		ux_err("failed to write %zu bytes to sahara\n", len);
		return -1;
	}

	return 0;
}

static int sahara_eoi(struct qdl_device *qdl, struct sahara_pkt *pkt)
{
	struct sahara_pkt done;

	if (pkt->length != SAHARA_END_OF_IMAGE_LENGTH) {
		ux_err("unexpected END_OF_IMAGE packet length %u\n", pkt->length);
		sahara_send_reset(qdl);
		return -1;
	}

	ux_debug("END OF IMAGE image: %d status: %d\n", pkt->eoi.image, pkt->eoi.status);

	if (pkt->eoi.status != 0) {
		ux_err("received non-successful end-of-image result\n");
		return -1;
	}

	done.cmd = SAHARA_DONE_CMD;
	done.length = SAHARA_DONE_LENGTH;
	qdl_write(qdl, &done, done.length, SAHARA_CMD_TIMEOUT_MS);
	return 0;
}

static int sahara_done(struct qdl_device *qdl, struct sahara_pkt *pkt)
{
	if (pkt->length != SAHARA_DONE_RESP_LENGTH) {
		ux_err("unexpected DONE_RESP packet length %u\n", pkt->length);
		sahara_send_reset(qdl);
		return -1;
	}

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
	char stem[PATH_MAX] = {};
	bool anymatch = false;
	const char *dot;
	char *ptr;
	char *tmp;
	char *s;

	if (!filter)
		return false;

	/* Build a stem (filename without extension) for bare-name filter tokens */
	dot = strrchr(filename, '.');
	if (dot && dot != filename) {
		size_t len = dot - filename;

		if (len < sizeof(stem)) {
			memcpy(stem, filename, len);
		}
	}

	tmp = strdup(filter);
	for (s = strtok_r(tmp, ",", &ptr); s; s = strtok_r(NULL, ",", &ptr)) {
		if (pattern_match(s, filename) || (stem[0] && pattern_match(s, stem))) {
			anymatch = true;
			break;
		}
	}
	free(tmp);

	return !anymatch;
}

static int write_zeroes(FILE *out, size_t count)
{
	static const char zeros[4096];
	size_t chunk;

	while (count > 0) {
		chunk = count > sizeof(zeros) ? sizeof(zeros) : count;
		if (fwrite(zeros, 1, chunk, out) != chunk)
			return -1;
		count -= chunk;
	}
	return 0;
}

static ssize_t copy_file_to(FILE *out, const char *path)
{
	char buf[65536];
	ssize_t written = 0;
	FILE *in;
	size_t n;

	in = fopen(path, "rb");
	if (!in)
		return -1;

	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) {
			fclose(in);
			return -1;
		}
		written += n;
	}
	fclose(in);
	return written;
}

/*
 * sahara_build_minidump_elf() - assemble a minidump ELF from downloaded regions
 * @ramdump_path: directory containing the downloaded region files
 * @filter:       the segment filter that was applied during download (may be NULL)
 * @table:        the Sahara debug region table received from the device
 * @n_regions:    number of entries in @table
 *
 * When the kernel minidump backend is active, one of the downloaded regions is
 * named "KELF". It is a fully formed ELF core header whose PT_LOAD program
 * headers describe the physical address and output offset of every other kernel
 * region. A second region ("Kvmcorein") carries the vmcoreinfo data that fills
 * the tail of the PT_NOTE section and has no PT_LOAD entry of its own.
 *
 * This function detects those two special regions, then iterates the PT_LOAD
 * headers in p_offset order, matches each one to a downloaded region file by
 * physical address, and concatenates everything into a single minidump.elf
 * ready for crash or gdb.
 *
 * All segments are written sequentially because meminspect assigns p_offset
 * values in a strictly increasing, contiguous order.
 */
static void sahara_build_minidump_elf(const char *ramdump_path, const char *filter,
				      const struct sahara_debug_region64 *table,
				      size_t n_regions)
{
	char path[PATH_MAX];
	char out_path[PATH_MAX];
	Elf64_Ehdr *ehdr;
	Elf64_Phdr *phdrs;
	void *kelf_buf = NULL;
	size_t kelf_size;
	ssize_t kelf_idx = -1;
	ssize_t vmcore_idx = -1;
	ssize_t written;
	FILE *out = NULL;
	FILE *in;
	size_t i, j;
	bool matched;

	/* Locate the KELF region; bail out silently if absent (not a minidump) */
	for (i = 0; i < n_regions; i++) {
		if (!strncmp(table[i].region, "md_KELF", 7)) {
			kelf_idx = i;
			break;
		}
	}

	if (kelf_idx < 0)
		return;

	/* If KELF itself was excluded by the filter, nothing to do */
	if (sahara_debug64_filter(table[kelf_idx].filename, filter))
		return;

	/* Read the KELF file into memory so we can parse its program headers */
	snprintf(path, sizeof(path), "%s/%s", ramdump_path, table[kelf_idx].filename);
	in = fopen(path, "rb");
	if (!in) {
		ux_err("minidump: failed to open KELF file %s\n", path);
		return;
	}

	long kelf_size_l;

	fseek(in, 0, SEEK_END);
	kelf_size_l = ftell(in);
	if (kelf_size_l <= 0) {
		ux_err("minidump: failed to get KELF file size\n");
		fclose(in);
		return;
	}
	fseek(in, 0, SEEK_SET);
	kelf_size = (size_t)kelf_size_l;

	kelf_buf = malloc(kelf_size);
	if (!kelf_buf) {
		fclose(in);
		return;
	}

	if (fread(kelf_buf, 1, kelf_size, in) != kelf_size) {
		ux_err("minidump: failed to read KELF file\n");
		fclose(in);
		goto out_free;
	}
	fclose(in);

	ehdr = kelf_buf;
	if (kelf_size < sizeof(*ehdr) ||
	    memcmp(ehdr->e_ident, ELFMAG, SELFMAG) ||
	    ehdr->e_type != ET_CORE ||
	    ehdr->e_phentsize != sizeof(Elf64_Phdr) ||
	    ehdr->e_phoff > kelf_size ||
	    (size_t)ehdr->e_phnum * sizeof(Elf64_Phdr) > kelf_size - ehdr->e_phoff) {
		ux_err("minidump: KELF does not look like a 64-bit ELF core file\n");
		goto out_free;
	}

	phdrs = (Elf64_Phdr *)((char *)kelf_buf + ehdr->e_phoff);

	/*
	 * Identify the vmcoreinfo region: among all K-prefixed regions, it is
	 * the one whose physical address has no matching PT_LOAD p_paddr entry.
	 * It fills the tail of the PT_NOTE section and must be placed immediately
	 * after the KELF content in the output file.
	 */
	for (i = 0; i < n_regions; i++) {
		if ((ssize_t)i == kelf_idx || strncmp(table[i].region, "md_K", 4) != 0)
			continue;

		matched = false;
		for (j = 0; j < ehdr->e_phnum; j++) {
			if (phdrs[j].p_type == PT_LOAD &&
			    phdrs[j].p_paddr == table[i].addr) {
				matched = true;
				break;
			}
		}

		if (!matched) {
			vmcore_idx = i;
			break;
		}
	}

	snprintf(out_path, sizeof(out_path), "%s/minidump.elf", ramdump_path);
	out = fopen(out_path, "wb");
	if (!out) {
		ux_err("minidump: failed to create %s\n", out_path);
		goto out_free;
	}

	/* 1. Write KELF: provides the ELF header, phdrs, and partial PT_NOTE */
	if (fwrite(kelf_buf, 1, kelf_size, out) != kelf_size) {
		ux_err("minidump: failed to write KELF to output\n");
		goto out_close;
	}

	/* 2. Write vmcoreinfo immediately after KELF to complete the PT_NOTE */
	if (vmcore_idx >= 0) {
		snprintf(path, sizeof(path), "%s/%s",
			 ramdump_path, table[vmcore_idx].filename);
		if (copy_file_to(out, path) < 0)
			ux_err("minidump: vmcoreinfo region missing, crash may not load correctly\n");
	}

	/*
	 * 3. Write PT_LOAD segments in ascending p_offset order.
	 *    meminspect assigns p_offset values contiguously in registration
	 *    order so iterating the phdr array gives the correct sequence.
	 */
	for (j = 0; j < ehdr->e_phnum; j++) {
		if (phdrs[j].p_type != PT_LOAD)
			continue;

		matched = false;
		for (i = 0; i < n_regions; i++) {
			if (table[i].addr != phdrs[j].p_paddr)
				continue;

			snprintf(path, sizeof(path), "%s/%s",
				 ramdump_path, table[i].filename);
			written = copy_file_to(out, path);
			if (written < 0) {
				ux_err("minidump: region %s missing, skipping\n",
				       table[i].filename);
				matched = true;
				break;
			}

			/* Zero-pad to p_filesz to keep offsets aligned */
			if ((size_t)written < phdrs[j].p_filesz &&
			    write_zeroes(out, phdrs[j].p_filesz - (size_t)written)) {
				ux_err("minidump: failed to pad region %s\n",
				       table[i].filename);
				goto out_close;
			}

			matched = true;
			break;
		}

		if (!matched)
			ux_err("minidump: no region found for PT_LOAD at paddr 0x%"
			       PRIx64 "\n", phdrs[j].p_paddr);
	}

	ux_info("minidump: ELF written to %s\n", out_path);

out_close:
	fclose(out);
out_free:
	free(kelf_buf);
}

static void sahara_debug64(struct qdl_device *qdl, struct sahara_pkt *pkt,
			   const char *ramdump_path, const char *filter)
{
	struct sahara_debug_region64 *table;
	struct sahara_pkt read_req;
	ssize_t n;
	size_t i;

	if (pkt->length != SAHARA_MEM_DEBUG64_LENGTH) {
		ux_err("unexpected MEM_DEBUG64 packet length %u\n", pkt->length);
		sahara_send_reset(qdl);
		return;
	}

	ux_debug("DEBUG64 address: 0x%" PRIx64 " length: 0x%" PRIx64 "\n",
		 pkt->debug64_req.addr, pkt->debug64_req.length);

	read_req.cmd = SAHARA_MEM_READ64_CMD;
	read_req.length = SAHARA_MEM_READ64_LENGTH;
	read_req.debug64_req.addr = pkt->debug64_req.addr;
	read_req.debug64_req.length = pkt->debug64_req.length;

	n = qdl_write(qdl, &read_req, read_req.length, SAHARA_CMD_TIMEOUT_MS);
	if (n < 0)
		return;

	if (read_req.debug64_req.length > 64 * 1024) {
		ux_err("DEBUG64 table length 0x%" PRIx64 " exceeds limit\n",
		       read_req.debug64_req.length);
		return;
	}

	table = malloc(read_req.debug64_req.length);
	if (!table)
		return;

	n = qdl_read(qdl, table, pkt->debug64_req.length, SAHARA_CMD_TIMEOUT_MS);
	if (n < 0) {
		free(table);
		return;
	}

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

	sahara_build_minidump_elf(ramdump_path, filter, table,
				  pkt->debug64_req.length / sizeof(table[0]));

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

static uint16_t sahara_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t sahara_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t sahara_le64(const uint8_t *p)
{
	return (uint64_t)sahara_le32(p) | ((uint64_t)sahara_le32(p + 4) << 32);
}

static void sahara_hexstr(const uint8_t *buf, size_t len, char *out, size_t out_sz)
{
	static const char hex[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < len && (2 * i + 2) < out_sz; i++) {
		out[2 * i] = hex[buf[i] >> 4];
		out[2 * i + 1] = hex[buf[i] & 0xf];
	}
	out[2 * i] = '\0';
}

/*
 * Normalise the raw OEM PK hash payload down to a single digest. Targets return
 * it in a few shapes: the bare digest, the digest repeated once per root
 * certificate, or the digest in a wider fixed-size field zero-padded to the
 * full width. Trim a repeated copy (detected by the first block recurring in
 * full), then strip trailing zero padding and round the length back up
 * to the nearest standard digest size (SHA-256/384/512), so a hash whose final
 * bytes are legitimately zero is not truncated. The rounding never grows the
 * result beyond the bytes actually read.
 */
static size_t sahara_pkhash_trim(const uint8_t *buf, size_t len)
{
	static const size_t digest_sizes[] = { 32, 48, 64 };
	size_t orig_len = len;
	size_t i;

	for (i = 4; i * 2 <= len; i++) {
		if (!memcmp(buf, buf + i, i)) {
			len = i;
			break;
		}
	}

	while (len > 0 && buf[len - 1] == 0)
		len--;

	for (i = 0; i < ARRAY_SIZE(digest_sizes); i++) {
		if (len <= digest_sizes[i] && digest_sizes[i] <= orig_len) {
			len = digest_sizes[i];
			break;
		}
	}

	return len;
}

static int sahara_read_payload(struct qdl_device *qdl, void *buf, size_t len)
{
	uint8_t *p = buf;
	size_t off = 0;
	int n;

	while (off < len) {
		n = qdl_read(qdl, p + off, len - off, SAHARA_CMD_TIMEOUT_MS);
		if (n < 0)
			return -1;
		if (n == 0)
			break;
		off += n;
	}

	return (int)off;
}

/*
 * Run one Sahara command-mode client command: send EXECUTE, learn the reply
 * size from EXECUTE_RESP, request the payload with EXECUTE_DATA and read it.
 */
static int sahara_command_exec(struct qdl_device *qdl, uint32_t client_cmd,
			       void *buf, size_t buf_len, size_t *out_len)
{
	struct sahara_pkt req = {};
	struct sahara_pkt *resp;
	uint8_t rxbuf[64];
	uint32_t data_len;
	int n;

	req.cmd = SAHARA_EXECUTE_CMD;
	req.length = SAHARA_EXECUTE_LENGTH;
	req.exec_req.client_cmd = client_cmd;
	if (qdl_write(qdl, &req, req.length, SAHARA_CMD_TIMEOUT_MS) < 0)
		return -1;

	n = qdl_read(qdl, rxbuf, sizeof(rxbuf), SAHARA_CMD_TIMEOUT_MS);
	if (n < 0)
		return -1;

	resp = (struct sahara_pkt *)rxbuf;
	if ((size_t)n < 4 * sizeof(uint32_t) || resp->cmd != SAHARA_EXECUTE_RESP_CMD) {
		ux_debug("Sahara: unexpected reply to exec cmd 0x%x\n", client_cmd);
		return -1;
	}

	data_len = resp->exec_resp.data_length;
	if (data_len == 0 || data_len > buf_len) {
		ux_debug("Sahara: exec cmd 0x%x reported invalid length %u\n",
			 client_cmd, data_len);
		return -1;
	}

	req.cmd = SAHARA_EXECUTE_DATA_CMD;
	req.length = SAHARA_EXECUTE_LENGTH;
	req.exec_req.client_cmd = client_cmd;
	if (qdl_write(qdl, &req, req.length, SAHARA_CMD_TIMEOUT_MS) < 0)
		return -1;

	n = sahara_read_payload(qdl, buf, data_len);
	if (n < 0 || (size_t)n < data_len)
		return -1;

	*out_len = data_len;
	return 0;
}

/*
 * Query and print the chip identity exposed over Sahara command mode. The set
 * of readable items depends on the protocol version: pre-v3 targets answer
 * MSM_HW_ID_READ, while v3 targets no longer do and instead expose the same
 * fields through READ_CHIP_ID_V3. The serial number and (where still permitted)
 * the OEM PK hash are read on all versions.
 */
static int sahara_command_info(struct qdl_device *qdl, unsigned int version)
{
	uint8_t payload[512];
	char hexbuf[2 * sizeof(payload) + 1];
	const char *pkhash = NULL;
	bool have_serial = false;
	bool have_hwid = false;
	uint32_t serial = 0;
	uint64_t hwid = 0;
	uint32_t msm_id = 0;
	unsigned int oem_id = 0;
	unsigned int model_id = 0;
	size_t len;

	if (sahara_command_exec(qdl, SAHARA_EXEC_CMD_SERIAL_NUM_READ,
				payload, sizeof(payload), &len) == 0 && len >= 4) {
		serial = sahara_le32(payload);
		have_serial = true;
	}

	if (version < 3) {
		if (sahara_command_exec(qdl, SAHARA_EXEC_CMD_MSM_HW_ID_READ,
					payload, sizeof(payload), &len) == 0 && len >= 8) {
			hwid = sahara_le64(payload);
			msm_id = hwid >> 32;
			oem_id = (hwid >> 16) & 0xffff;
			model_id = hwid & 0xffff;
			have_hwid = true;
		}
	} else {
		if (sahara_command_exec(qdl, SAHARA_EXEC_CMD_READ_CHIP_ID_V3,
					payload, sizeof(payload), &len) == 0 && len >= 44) {
			msm_id = sahara_le32(payload + 36);
			oem_id = sahara_le16(payload + 40);
			model_id = sahara_le16(payload + 42);
			/* Some v3 targets carry the OEM ID in an alternate slot */
			if (oem_id == 0 && len >= 46)
				oem_id = sahara_le16(payload + 44);
			hwid = ((uint64_t)msm_id << 32) |
			       ((uint64_t)oem_id << 16) | model_id;
			have_hwid = true;
		}
	}

	if (sahara_command_exec(qdl, SAHARA_EXEC_CMD_OEM_PK_HASH_READ,
				payload, sizeof(payload), &len) == 0 && len > 0) {
		len = sahara_pkhash_trim(payload, len);
		sahara_hexstr(payload, len, hexbuf, sizeof(hexbuf));
		pkhash = hexbuf;
	}

	if (!have_serial && !have_hwid && !pkhash) {
		ux_err("device did not return any chip identity information\n");
		return -1;
	}

	ux_info("Sahara protocol version: %u\n", version);
	if (have_serial)
		ux_info("Chip serial number:      0x%08x\n", serial);
	if (have_hwid)
		ux_info("HW ID:                   0x%016" PRIx64
			" (MSM_ID:0x%08x, OEM_ID:0x%04x, MODEL_ID:0x%04x)\n",
			hwid, msm_id, oem_id, model_id);
	if (pkhash)
		ux_info("OEM PK hash:             0x%s\n", pkhash);

	return 0;
}

static void sahara_send_switch_mode(struct qdl_device *qdl, unsigned int mode)
{
	struct sahara_pkt pkt = {};

	pkt.cmd = SAHARA_SWITCH_MODE_CMD;
	pkt.length = SAHARA_SWITCH_MODE_LENGTH;
	pkt.switch_mode.mode = mode;

	qdl_write(qdl, &pkt, pkt.length, SAHARA_CMD_TIMEOUT_MS);
}

/*
 * Read and print the chip identity (serial, HW ID and OEM PK hash) by entering
 * Sahara command mode. Unlike the image-transfer path this requests
 * SAHARA_MODE_COMMAND in the HELLO response and drives EXECUTE transactions.
 * Command mode is left by switching back to image transfer, which returns the
 * device to its initial HELLO state.
 */
int sahara_chipinfo(struct qdl_device *qdl)
{
	unsigned int version = SAHARA_VERSION;
	struct sahara_pkt *pkt;
	char buf[4096];
	int ret;
	int n;

	if (qdl->dev_type == QDL_DEVICE_SIM)
		return 0;

	n = qdl_read(qdl, buf, sizeof(buf), SAHARA_CMD_TIMEOUT_MS);

	/* A Firehose programmer is already running; chip info needs Sahara */
	if (n >= 5 && !memcmp(buf, "<?xml", 5)) {
		ux_err("device is already in Firehose mode; chip info is only available via Sahara\n");
		return -1;
	}

	if (n < 0) {
		/*
		 * The QUD driver eats the HELLO request on many targets; recover
		 * by prodding the device with an unsolicited command-mode HELLO
		 * response. The version is unknown here, so assume the baseline.
		 */
		if (n != -ETIMEDOUT ||
		    (qdl->dev_type != QDL_DEVICE_QUD && qdl->dev_type != QDL_DEVICE_AUTO)) {
			ux_err("failed to read Sahara HELLO from device\n");
			return -1;
		}
		sahara_send_hello_resp(qdl, SAHARA_VERSION, SAHARA_MODE_COMMAND);
	} else {
		pkt = (struct sahara_pkt *)buf;
		if ((uint32_t)n != pkt->length || pkt->cmd != SAHARA_HELLO_CMD) {
			ux_err("unexpected Sahara packet 0x%x while waiting for HELLO\n",
			       pkt->cmd);
			return -1;
		}

		version = pkt->hello_req.version;
		ux_debug("Sahara HELLO version %u mode %u\n",
			 version, pkt->hello_req.mode);
		sahara_send_hello_resp(qdl, version, SAHARA_MODE_COMMAND);
	}

	n = qdl_read(qdl, buf, sizeof(buf), SAHARA_CMD_TIMEOUT_MS);
	if (n < 0) {
		ux_err("no Sahara CMD_READY received; device may not support command mode\n");
		return -1;
	}

	pkt = (struct sahara_pkt *)buf;
	if (pkt->cmd != SAHARA_CMD_READY_CMD) {
		if (pkt->cmd == SAHARA_END_OF_IMAGE_CMD)
			ux_err("device rejected command mode (end-of-image status %u)\n",
			       pkt->eoi.status);
		else
			ux_err("unexpected Sahara packet 0x%x while entering command mode\n",
			       pkt->cmd);
		return -1;
	}

	ret = sahara_command_info(qdl, version);

	/*
	 * Switch back to image-transfer mode so the device re-issues its HELLO
	 * and stays usable for a subsequent query or flash, without a reset.
	 */
	sahara_send_switch_mode(qdl, SAHARA_MODE_IMAGE_TX_PENDING);

	return ret;
}

int sahara_run(struct qdl_device *qdl, const struct sahara_image *images,
	       const char *ramdump_path,
	       const char *ramdump_filter)
{
	/*
	 * Auto-detect that the device is already running the Firehose
	 * programmer (e.g. left running by a previous --skip-reset
	 * invocation): if the first read times out or returns a Firehose XML
	 * banner instead of a Sahara HELLO, skip Sahara entirely. Disabled
	 * in ramdump mode, where Sahara is the only valid protocol.
	 */
	const bool detect_firehose = !ramdump_path;
	bool first_read = true;
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
			if (first_read && detect_firehose && n == -ETIMEDOUT) {
				/*
				 * The QUD driver will eat the HELLO request on
				 * many modern targets, so send an unsolicited
				 * HELLO response.
				 * If the device is already in Firehose mode,
				 * the programmer will fail to parse the "XML"
				 * message and report an error, which will
				 * trigger below detection of a <?xml response.
				 */
				if (qdl->dev_type == QDL_DEVICE_QUD || qdl->dev_type == QDL_DEVICE_AUTO) {
					sahara_send_hello_resp(qdl, SAHARA_VERSION, 0);
					continue;
				}
				ux_info("no Sahara HELLO received; assuming Firehose programmer is already running\n");
				return 0;
			}
			ux_err("failed to read sahara request from device\n");
			break;
		}

		if (first_read && detect_firehose &&
		    n >= 5 && !memcmp(buf, "<?xml", 5)) {
			ux_info("device is already in Firehose mode, skipping Sahara\n");
			return 0;
		}
		first_read = false;

		pkt = (struct sahara_pkt *)buf;
		if ((uint32_t)n != pkt->length) {
			ux_err("request length not matching received request\n");
			return -EINVAL;
		}

		switch (pkt->cmd) {
		case SAHARA_HELLO_CMD:
			if (sahara_hello(qdl, pkt) < 0)
				return -1;
			break;
		case SAHARA_READ_DATA_CMD:
			if (sahara_read(qdl, pkt, images) < 0)
				return -1;
			break;
		case SAHARA_END_OF_IMAGE_CMD:
			if (sahara_eoi(qdl, pkt) < 0)
				return -1;
			break;
		case SAHARA_DONE_RESP_CMD:
			n = sahara_done(qdl, pkt);
			if (n < 0)
				return -1;
			done = n;

			/* E.g MSM8916 EDL reports done = 0 here */
			if (sahara_has_done_pending_quirk(images))
				done = true;
			break;
		case SAHARA_MEM_DEBUG64_CMD:
			sahara_debug64(qdl, pkt, ramdump_path, ramdump_filter);
			break;
		case SAHARA_READ_DATA64_CMD:
			if (sahara_read64(qdl, pkt, images) < 0)
				return -1;
			break;
		case SAHARA_RESET_RESP_CMD:
			if (pkt->length != SAHARA_RESET_LENGTH) {
				ux_err("unexpected RESET_RESP packet length %u\n", pkt->length);
				return -1;
			}
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
