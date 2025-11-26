// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <stdlib.h>
#include <string.h>
#define _FILE_OFFSET_BITS 64
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "qdl.h"
#include "gpt.h"

struct gpt_guid {
	uint32_t data1;
	uint16_t data2;
	uint16_t data3;
	uint8_t  data4[8];
} __attribute__((packed));

static const struct gpt_guid gpt_zero_guid = {0};

struct gpt_header {
	uint8_t signature[8];
	uint32_t revision;
	uint32_t header_size;
	uint32_t header_crc32;
	uint32_t reserved;
	uint64_t current_lba;
	uint64_t backup_lba;
	uint64_t first_usable_lba;
	uint64_t last_usable_lba;
	struct gpt_guid disk_guid;
	uint64_t part_entry_lba;
	uint32_t num_part_entries;
	uint32_t part_entry_size;
	uint32_t part_array_crc32;
	uint8_t reserved2[420];
} __attribute__((packed));

struct gpt_entry {
	struct gpt_guid type_guid;
	struct gpt_guid unique_guid;
	uint64_t first_lba;
	uint64_t last_lba;
	uint64_t attrs;
	uint16_t name_utf16le[36];
} __attribute__((packed));

struct gpt_partition {
	const char *name;
	unsigned int partition;
	unsigned int start_sector;
	unsigned int num_sectors;

	struct gpt_partition *next;
};

static struct gpt_partition *gpt_partitions;
static struct gpt_partition *gpt_partitions_last;

static void utf16le_to_utf8(uint16_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
	uint32_t codepoint;
	uint16_t high;
	uint16_t low;
	uint16_t w;
	size_t i;
	size_t j = 0;

	for (i = 0; i < in_len; i++) {
		w = in[i];

		if (w >= 0xd800 && w <= 0xdbff) {
			high = w - 0xd800;

			if (i < in_len) {
				w = in[++i];
				if (w >= 0xdc00 && w <= 0xdfff) {
					low = w - 0xdc00;
					codepoint = (((uint32_t)high << 10) | low) + 0x10000;
				} else {
					/* Surrogate without low surrogate */
					codepoint = 0xfffd;
				}
			} else {
				/* Lone high surrogate at end of string */
				codepoint = 0xfffd;
			}
		} else if (w >= 0xdc00 && w <= 0xdfff) {
			/* Low surrogate without high */
			codepoint = 0xfffd;
		} else {
			codepoint = w;
		}

		if (codepoint == 0)
			break;

		if (codepoint <= 0x7f) {
			if (j + 1 >= out_len)
				break;
			out[j++] = (uint8_t)codepoint;
		} else if (codepoint <= 0x7ff) {
			if (j + 2 >= out_len)
				break;
			out[j++] = 0xc0 | ((codepoint >> 6) & 0x1f);
			out[j++] = 0x80 | (codepoint & 0x3f);
		} else if (codepoint <= 0xffff) {
			if (j + 3 >= out_len)
				break;
			out[j++] = 0xe0 | ((codepoint >> 12) & 0x0f);
			out[j++] = 0x80 | ((codepoint >> 6) & 0x3f);
			out[j++] = 0x80 | (codepoint & 0x3f);
		} else if (codepoint <= 0x10ffff) {
			if (j + 4 >= out_len)
				break;
			out[j++] = 0xf0 | ((codepoint >> 18) & 0x07);
			out[j++] = 0x80 | ((codepoint >> 12) & 0x3f);
			out[j++] = 0x80 | ((codepoint >> 6) & 0x3f);
			out[j++] = 0x80 | (codepoint & 0x3f);
		}
	}

	out[j] = '\0';
}

static int gpt_load_table_from_partition(struct qdl_device *qdl, unsigned int phys_partition, bool *eof)
{
	struct gpt_partition *partition;
	struct gpt_entry *entry;
	struct gpt_header gpt;
	uint8_t buf[4096];
	struct read_op op;
	unsigned int offset;
	unsigned int lba;
	char lba_buf[10];
	uint16_t name_utf16le[36];
	char name[36 * 4];
	int ret;
	unsigned int i;

	memset(&op, 0, sizeof(op));

	op.sector_size = qdl->sector_size;
	op.start_sector = "1";
	op.num_sectors = 1;
	op.partition = phys_partition;

	memset(&buf, 0, sizeof(buf));
	ret = firehose_read_buf(qdl, &op, &gpt, sizeof(gpt));
	if (ret) {
		/* Assume that we're beyond the last partition */
		*eof = true;
		return -1;
	}

	if (memcmp(gpt.signature, "EFI PART", 8)) {
		ux_err("partition %d has not GPT header\n", phys_partition);
		return 0;
	}

	if (gpt.part_entry_size > qdl->sector_size || gpt.num_part_entries > 1024) {
		ux_debug("partition %d has invalid GPT header\n", phys_partition);
		return -1;
	}

	ux_debug("Loading GPT table from physical partition %d\n", phys_partition);
	for (i = 0; i < gpt.num_part_entries; i++) {
		offset = (i * gpt.part_entry_size) % qdl->sector_size;

		if (offset == 0) {
			lba = gpt.part_entry_lba + i * gpt.part_entry_size / qdl->sector_size;
			sprintf(lba_buf, "%u", lba);
			op.start_sector = lba_buf;

			memset(buf, 0, sizeof(buf));
			ret = firehose_read_buf(qdl, &op, buf, sizeof(buf));
			if (ret) {
				ux_err("failed to read GPT partition entries from %d:%u\n", phys_partition, lba);
				return -1;
			}
		}

		entry = (struct gpt_entry *)(buf + offset);

		if (!memcmp(&entry->type_guid, &gpt_zero_guid, sizeof(struct gpt_guid)))
			continue;

		memcpy(name_utf16le, entry->name_utf16le, sizeof(name_utf16le));
		utf16le_to_utf8(name_utf16le, 36, (uint8_t *)name, sizeof(name));

		partition = calloc(1, sizeof(*partition));
		partition->name = strdup(name);
		partition->partition = phys_partition;
		partition->start_sector = entry->first_lba;
		/* if first_lba == last_lba there is 1 sector worth of data (IE: add 1 below) */
		partition->num_sectors = entry->last_lba - entry->first_lba + 1;

		ux_debug("  %3d: %s start sector %u, num sectors %u\n", i, partition->name,
			 partition->start_sector, partition->num_sectors);

		if (gpt_partitions) {
			gpt_partitions_last->next = partition;
			gpt_partitions_last = partition;
		} else {
			gpt_partitions = partition;
			gpt_partitions_last = partition;
		}
	}

	return 0;
}

static int gpt_load_tables(struct qdl_device *qdl)
{
	unsigned int i;
	bool eof = false;
	int ret = 0;

	if (gpt_partitions)
		return 0;

	for (i = 0; ; i++) {
		ret = gpt_load_table_from_partition(qdl, i, &eof);
		if (ret)
			break;
	}

	return eof ? 0 : ret;
}

int gpt_find_by_name(struct qdl_device *qdl, const char *name, int *phys_partition,
		     unsigned int *start_sector, unsigned int *num_sectors)
{
	struct gpt_partition *gpt_part;
	bool found = false;
	int ret;

	if (qdl->dev_type == QDL_DEVICE_SIM)
		return 0;

	ret = gpt_load_tables(qdl);
	if (ret < 0)
		return -1;

	for (gpt_part = gpt_partitions; gpt_part; gpt_part = gpt_part->next) {
		if (*phys_partition >= 0 && gpt_part->partition != *phys_partition)
			continue;

		if (strcmp(gpt_part->name, name))
			continue;

		if (found) {
			ux_err("duplicate candidates for partition \"%s\" found\n", name);
			return -1;
		}

		*phys_partition = gpt_part->partition;
		*start_sector = gpt_part->start_sector;
		*num_sectors = gpt_part->num_sectors;

		found = true;
	}

	if (!found) {
		if (*phys_partition >= 0)
			ux_err("no partition \"%s\" found on physical partition %d\n", name, *phys_partition);
		else
			ux_err("no partition \"%s\" found\n", name);
		return -1;
	}

	return 0;
}
