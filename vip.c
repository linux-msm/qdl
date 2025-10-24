// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sim.h"

#define DIGEST_FULL_TABLE_FILE			"DIGEST_TABLE.bin"
#define CHAINED_TABLE_FILE_PREF			"ChainedTableOfDigests"
#define CHAINED_TABLE_FILE_MAX_NAME		64
#define DIGEST_TABLE_TO_SIGN_FILE		"DigestsToSign.bin"
#define DIGEST_TABLE_TO_SIGN_FILE_MBN		(DIGEST_TABLE_TO_SIGN_FILE ".mbn")
#define MAX_DIGESTS_PER_SIGNED_FILE		54
#define MAX_DIGESTS_PER_SIGNED_TABLE		(MAX_DIGESTS_PER_SIGNED_FILE - 1)
#define MAX_DIGESTS_PER_CHAINED_FILE		256
#define MAX_DIGESTS_PER_CHAINED_TABLE		(MAX_DIGESTS_PER_CHAINED_FILE - 1)
#define MAX_DIGESTS_PER_BUF			16

#ifndef O_BINARY
#define O_BINARY  0
#define O_TEXT    0
#endif

struct vip_table_generator {
	unsigned char hash[SHA256_DIGEST_LENGTH];

	SHA2_CTX ctx;

	FILE *digest_table_fd;
	size_t digest_num_written;

	const char *path;
};

static void print_digest(unsigned char *buf)
{
	char hex_str[SHA256_DIGEST_STRING_LENGTH];

	for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i)
		sprintf(hex_str + i * 2, "%02x", buf[i]);

	hex_str[SHA256_DIGEST_STRING_LENGTH - 1] = '\0';

	ux_debug("FIREHOSE PACKET SHA256: %s\n", hex_str);
}

int vip_gen_init(struct qdl_device *qdl, const char *path)
{
	struct vip_table_generator *vip_gen;
	struct stat st;
	char filepath[PATH_MAX];

	if (qdl->dev_type != QDL_DEVICE_SIM) {
		ux_err("Should be executed in simulation dry-run mode\n");
		return -1;
	}

	if (stat(path, &st) || !S_ISDIR(st.st_mode)) {
		ux_err("Directory '%s' to store VIP tables doesn't exist\n", path);
	}

	vip_gen = malloc(sizeof(struct vip_table_generator));
	if (!vip_gen) {
		ux_err("Can't allocate memory for vip_table_generator\n");
		return -1;
	}
	if (!sim_set_digest_generation(true, qdl, vip_gen)) {
		ux_err("Can't enable digest table generation\n");
		goto out_cleanup;
	}
	vip_gen->digest_num_written = 0;
	vip_gen->path = path;

	snprintf(filepath, sizeof(filepath), "%s/%s", path, DIGEST_FULL_TABLE_FILE);

	vip_gen->digest_table_fd = fopen(filepath, "wb");
	if (!vip_gen->digest_table_fd) {
		ux_err("Can't create %s file\n", filepath);
		goto out_cleanup;
	}

	return 0;
out_cleanup:
	free(vip_gen);
	sim_set_digest_generation(false, qdl, NULL);

	return -1;
}

void vip_gen_chunk_init(struct qdl_device *qdl)
{
	struct vip_table_generator *vip_gen;

	vip_gen = sim_get_vip_generator(qdl);
	if (!vip_gen)
		return;

	SHA256Init(&vip_gen->ctx);
}

void vip_gen_chunk_update(struct qdl_device *qdl, const void *buf, size_t len)
{
	struct vip_table_generator *vip_gen;

	vip_gen = sim_get_vip_generator(qdl);
	if (!vip_gen)
		return;

	SHA256Update(&vip_gen->ctx, (uint8_t *)buf, len);
}

void vip_gen_chunk_store(struct qdl_device *qdl)
{
	struct vip_table_generator *vip_gen;

	vip_gen = sim_get_vip_generator(qdl);
	if (!vip_gen)
		return;

	SHA256Final(vip_gen->hash, &vip_gen->ctx);

	print_digest(vip_gen->hash);

	if (fwrite(vip_gen->hash, SHA256_DIGEST_LENGTH, 1, vip_gen->digest_table_fd) != 1) {
		ux_err("Failed to write digest to the " DIGEST_FULL_TABLE_FILE);
		goto out_cleanup;
	}

	vip_gen->digest_num_written++;

	return;

out_cleanup:
	fclose(vip_gen->digest_table_fd);
}

static int write_output_file(const char *filename, bool append, const void *data, size_t len)
{
	FILE *fp;
	char *mode = "wb";

	if (append)
		mode = "ab";

	fp = fopen(filename, mode);
	if (!fp) {
		ux_err("Failed to open file for appending\n");
		return -1;
	}

	if (fwrite(data, 1, len, fp) != len) {
		ux_err("Failed to append to file\n");
		fclose(fp);
		return -1;
	}

	fclose(fp);

	return 0;
}

static int calculate_hash_of_file(const char *filename, unsigned char *hash)
{
	unsigned char buf[1024];
	SHA2_CTX ctx;

	FILE *fp = fopen(filename, "rb");

	if (!fp) {
		ux_err("Failed to open file for hashing\n");
		return -1;
	}

	SHA256Init(&ctx);

	size_t bytes;

	while ((bytes = fread(buf, 1, sizeof(buf), fp)) > 0) {
		SHA256Update(&ctx, (uint8_t *)buf, bytes);
	}

	fclose(fp);

	SHA256Final(hash, &ctx);

	return 0;
}

static int write_digests_to_table(char *src_table, char *dest_table, size_t start_digest, size_t count)
{
	const size_t elem_size = SHA256_DIGEST_LENGTH;
	unsigned char buf[MAX_DIGESTS_PER_BUF * SHA256_DIGEST_LENGTH];
	size_t written = 0;
	int ret;

	int fd = open(src_table, O_RDONLY | O_BINARY);

	if (fd < 0) {
		ux_err("Failed to open %s for reading\n", src_table);
		return -1;
	}

	/* Seek to offset of start_digest */
	size_t offset = elem_size * start_digest;

	if (lseek(fd, offset, SEEK_SET) != offset) {
		ux_err("Failed to seek in %s\n", src_table);
		goto out_cleanup;
	}

	while (written < (count * elem_size)) {
		size_t to_read = count * elem_size - written;

		if (to_read > sizeof(buf))
			to_read = sizeof(buf);

		ssize_t bytes = read(fd, buf, to_read);

		if (bytes < 0 || (size_t)bytes != to_read) {
			ux_err("Failed to read from %s\n", src_table);
			goto out_cleanup;
		}

		ret = write_output_file(dest_table, (written != 0), buf, bytes);
		if (ret < 0) {
			ux_err("Can't write digests to %s\n", dest_table);
			goto out_cleanup;
		}

		written += to_read;
	}
	close(fd);

	return 0;
out_cleanup:
	close(fd);

	return -1;
}

static int create_chained_tables(struct vip_table_generator *vip_gen)
{
	size_t chained_num = 0;
	size_t tosign_count = 0;
	size_t total_digests = vip_gen->digest_num_written;
	char src_table[PATH_MAX];
	char dest_table[PATH_MAX];
	unsigned char hash[SHA256_DIGEST_LENGTH];
	int ret;

	snprintf(src_table, sizeof(src_table), "%s/%s", vip_gen->path, DIGEST_FULL_TABLE_FILE);

	/* Step 1: Write digest table to DigestsToSign.bin */
	snprintf(dest_table, sizeof(dest_table), "%s/%s",
		 vip_gen->path, DIGEST_TABLE_TO_SIGN_FILE);
	tosign_count = total_digests < MAX_DIGESTS_PER_SIGNED_TABLE ? total_digests :
		       MAX_DIGESTS_PER_SIGNED_TABLE;

	ret = write_digests_to_table(src_table, dest_table, 0, tosign_count);
	if (ret) {
		ux_err("Writing digests to %s failed\n", dest_table);
		return ret;
	}

	/* Step 2: Write remaining digests to ChainedTableOfDigests<n>.bin */
	if (total_digests > MAX_DIGESTS_PER_SIGNED_TABLE) {
		size_t remaining_digests = total_digests - MAX_DIGESTS_PER_SIGNED_TABLE;

		while (remaining_digests > 0) {
			size_t table_digests = remaining_digests > MAX_DIGESTS_PER_CHAINED_TABLE ?
						MAX_DIGESTS_PER_CHAINED_TABLE : remaining_digests;

			snprintf(dest_table, sizeof(dest_table),
				 "%s/%s%zu.bin", vip_gen->path,
				 CHAINED_TABLE_FILE_PREF, chained_num);

			ret = write_digests_to_table(src_table, dest_table,
						     total_digests - remaining_digests,
						     table_digests);
			if (ret) {
				ux_err("Writing digests to %s failed\n", dest_table);
				return ret;
			}

			remaining_digests -= table_digests;
			if (!remaining_digests) {
				/* Add zero (the packet can't be multiple of 512 bytes) */
				ret = write_output_file(dest_table, true, "\0", 1);
				if (ret < 0) {
					ux_err("Can't write 0 to %s\n", dest_table);
					return ret;
				}
			}
			chained_num++;
		}
	}

	/* Step 3: Recursively hash and append backwards */
	for (ssize_t i = chained_num - 1; i >= 0; --i) {
		snprintf(src_table, sizeof(src_table),
			 "%s/%s%zd.bin", vip_gen->path,
			 CHAINED_TABLE_FILE_PREF, i);
		ret = calculate_hash_of_file(src_table, hash);
		if (ret < 0) {
			ux_err("Failed to hash %s\n", src_table);
			return ret;
		}

		if (i == 0) {
			snprintf(dest_table, sizeof(dest_table), "%s/%s",
				 vip_gen->path, DIGEST_TABLE_TO_SIGN_FILE);
			ret = write_output_file(dest_table, true, hash, SHA256_DIGEST_LENGTH);
			if (ret < 0) {
				ux_err("Failed to append hash to %s\n", dest_table);
				return ret;
			}
		} else {
			snprintf(dest_table, sizeof(dest_table),
				 "%s/%s%zd.bin", vip_gen->path,
				 CHAINED_TABLE_FILE_PREF, (i - 1));
			ret = write_output_file(dest_table, true, hash, SHA256_DIGEST_LENGTH);
			if (ret < 0) {
				ux_err("Failed to append hash to %s\n", dest_table);
				return ret;
			}
		}
	}

	return 0;
}

void vip_gen_finalize(struct qdl_device *qdl)
{
	struct vip_table_generator *vip_gen;

	vip_gen = sim_get_vip_generator(qdl);
	if (!vip_gen)
		return;

	fclose(vip_gen->digest_table_fd);

	ux_debug("VIP TABLE DIGESTS: %lu\n", vip_gen->digest_num_written);

	if (create_chained_tables(vip_gen) < 0)
		ux_err("Error occurred when creating table of digests\n");

	free(vip_gen);
	sim_set_digest_generation(false, qdl, NULL);
}

int vip_transfer_init(struct qdl_device *qdl, const char *vip_table_path)
{
	char fullpath[PATH_MAX];

	snprintf(fullpath, sizeof(fullpath), "%s/%s",
		 vip_table_path, DIGEST_TABLE_TO_SIGN_FILE_MBN);
	qdl->vip_data.signed_table_fd = open(fullpath, O_RDONLY);
	if (!qdl->vip_data.signed_table_fd) {
		ux_err("Can't open signed table %s\n", fullpath);
		return -1;
	}

	qdl->vip_data.chained_num = 0;

	for (int i = 0; i < MAX_CHAINED_FILES; ++i) {
		snprintf(fullpath, sizeof(fullpath), "%s/%s%d%s",
			 vip_table_path, CHAINED_TABLE_FILE_PREF, i, ".bin");

		int fd = open(fullpath, O_RDONLY);

		if (fd == -1) {
			if (errno == ENOENT)
				break;

			ux_err("Can't open signed table %s\n", fullpath);
			goto out_cleanup;
		}

		qdl->vip_data.chained_fds[qdl->vip_data.chained_num++] = fd;
	}

	qdl->vip_data.state = VIP_INIT;
	qdl->vip_data.chained_cur = 0;

	return 0;

out_cleanup:
	close(qdl->vip_data.signed_table_fd);
	for (int i = 0; i < qdl->vip_data.chained_num - 1; ++i)
		close(qdl->vip_data.chained_fds[i]);
	return -1;
}

void vip_transfer_deinit(struct qdl_device *qdl)
{
	close(qdl->vip_data.signed_table_fd);
	for (int i = 0; i < qdl->vip_data.chained_num - 1; ++i)
		close(qdl->vip_data.chained_fds[i]);
}

static int vip_transfer_send_raw(struct qdl_device *qdl, int table_fd)
{
	struct stat sb;
	int ret;
	void *buf;
	size_t n;

	ret = fstat(table_fd, &sb);
	if (ret < 0) {
		ux_err("Failed to stat digest table file\n");
		return -1;
	}

	buf = malloc(sb.st_size);
	if (!buf) {
		ux_err("Failed to allocate transfer buffer\n");
		return -1;
	}

	n = read(table_fd, buf, sb.st_size);
	if (n < 0) {
		ux_err("failed to read binary\n");
		ret = -1;
		goto out;
	}

	n = qdl_write(qdl, buf, sb.st_size, 1000);
	if (n < 0) {
		ux_err("USB write failed for data chunk\n");
		ret = -1;
		goto out;
	}

out:
	free(buf);

	return ret;
}

int vip_transfer_handle_tables(struct qdl_device *qdl)
{
	struct vip_transfer_data *vip_data = &qdl->vip_data;
	int ret = 0;

	if (vip_data->state == VIP_DISABLED)
		return 0;

	if (vip_data->state == VIP_INIT) {
		/* Send initial signed table */
		ret = vip_transfer_send_raw(qdl, vip_data->signed_table_fd);
		if (ret) {
			ux_err("VIP: failed to send the Signed VIP table\n");
			return ret;
		}
		ux_debug("VIP: successfully sent the Initial VIP table\n");

		vip_data->state = VIP_SEND_DATA;
		vip_data->frames_sent = 0;
		vip_data->frames_left = MAX_DIGESTS_PER_SIGNED_TABLE;
		vip_data->fh_parse_status = true;
	}
	if (vip_data->state == VIP_SEND_NEXT_TABLE) {
		if (vip_data->chained_cur >= vip_data->chained_num) {
			ux_err("VIP: the required quantity of chained tables is missing\n");
			return -1;
		}
		ret = vip_transfer_send_raw(qdl, vip_data->chained_fds[vip_data->chained_cur]);
		if (ret) {
			ux_err("VIP: failed to send the chained VIP table\n");
			return ret;
		}

		ux_debug("VIP: successfully sent " CHAINED_TABLE_FILE_PREF "%lu.bin\n",
			 vip_data->chained_cur);

		vip_data->state = VIP_SEND_DATA;
		vip_data->frames_sent = 0;
		vip_data->frames_left = MAX_DIGESTS_PER_CHAINED_TABLE;
		vip_data->fh_parse_status = true;
		vip_data->chained_cur++;
	}

	vip_data->frames_sent++;
	if (vip_data->frames_sent >= vip_data->frames_left)
		vip_data->state = VIP_SEND_NEXT_TABLE;

	return 0;
}

bool vip_transfer_status_check_needed(struct qdl_device *qdl)
{
	return qdl->vip_data.fh_parse_status;
}

void vip_transfer_clear_status(struct qdl_device *qdl)
{
	qdl->vip_data.fh_parse_status = false;
}
