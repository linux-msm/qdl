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

void vip_transfer_deinit(struct qdl_device *qdl);

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
		return -1;
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
	vip_gen->digest_table_fd = NULL;
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

static int write_digests_to_table(char *src_table, char *dest_table, size_t start_digest,
				  size_t count, SHA2_CTX *out_ctx)
{
	const size_t elem_size = SHA256_DIGEST_LENGTH;
	unsigned char buf[MAX_DIGESTS_PER_BUF * SHA256_DIGEST_LENGTH];
	size_t written = 0;
	FILE *out = NULL;
	int ret = -1;

	int fd = open(src_table, O_RDONLY | O_BINARY);

	if (fd < 0) {
		ux_err("Failed to open %s for reading\n", src_table);
		return -1;
	}

	out = fopen(dest_table, "wb");
	if (!out) {
		ux_err("Failed to open %s for writing\n", dest_table);
		goto out_cleanup;
	}

	if (out_ctx)
		SHA256Init(out_ctx);

	/* Seek to offset of start_digest */
	off_t offset = elem_size * start_digest;

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

		if (fwrite(buf, 1, bytes, out) != (size_t)bytes) {
			ux_err("Can't write digests to %s\n", dest_table);
			goto out_cleanup;
		}

		if (out_ctx)
			SHA256Update(out_ctx, buf, bytes);

		written += to_read;
	}
	ret = 0;

out_cleanup:
	if (out)
		fclose(out);
	close(fd);

	return ret;
}

static int create_chained_tables(struct vip_table_generator *vip_gen)
{
	size_t total_digests = vip_gen->digest_num_written;
	char src_table[PATH_MAX];
	char dest_table[PATH_MAX];
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA2_CTX *chain_ctxs = NULL;
	size_t chained_num = 0;
	int ret = 0;

	snprintf(src_table, sizeof(src_table), "%s/%s", vip_gen->path, DIGEST_FULL_TABLE_FILE);

	/* Pre-compute the number of chained tables so we can allocate contexts */
	if (total_digests > MAX_DIGESTS_PER_SIGNED_TABLE) {
		size_t remaining = total_digests - MAX_DIGESTS_PER_SIGNED_TABLE;

		chained_num = (remaining + MAX_DIGESTS_PER_CHAINED_TABLE - 1) /
			      MAX_DIGESTS_PER_CHAINED_TABLE;
		chain_ctxs = calloc(chained_num, sizeof(SHA2_CTX));
		if (!chain_ctxs)
			return -1;
	}

	/* Step 1: Write digest table to DigestsToSign.bin */
	snprintf(dest_table, sizeof(dest_table), "%s/%s",
		 vip_gen->path, DIGEST_TABLE_TO_SIGN_FILE);
	size_t tosign_count = total_digests < MAX_DIGESTS_PER_SIGNED_TABLE ? total_digests :
			      MAX_DIGESTS_PER_SIGNED_TABLE;

	ret = write_digests_to_table(src_table, dest_table, 0, tosign_count, NULL);
	if (ret) {
		ux_err("Writing digests to %s failed\n", dest_table);
		goto out;
	}

	/* Step 2: Write remaining digests to ChainedTableOfDigests<n>.bin, capturing
	 * a SHA2 context after writing each table's digest payload so Step 3 can
	 * compute the final hashes without re-reading the files.
	 */
	if (total_digests > MAX_DIGESTS_PER_SIGNED_TABLE) {
		size_t remaining_digests = total_digests - MAX_DIGESTS_PER_SIGNED_TABLE;
		size_t chain_idx = 0;

		while (remaining_digests > 0) {
			size_t table_digests = remaining_digests > MAX_DIGESTS_PER_CHAINED_TABLE ?
						MAX_DIGESTS_PER_CHAINED_TABLE : remaining_digests;

			snprintf(dest_table, sizeof(dest_table),
				 "%s/%s%zu.bin", vip_gen->path,
				 CHAINED_TABLE_FILE_PREF, chain_idx);

			ret = write_digests_to_table(src_table, dest_table,
						     total_digests - remaining_digests,
						     table_digests, &chain_ctxs[chain_idx]);
			if (ret) {
				ux_err("Writing digests to %s failed\n", dest_table);
				goto out;
			}

			remaining_digests -= table_digests;
			if (!remaining_digests) {
				/* Add zero (the packet can't be multiple of 512 bytes) */
				ret = write_output_file(dest_table, true, "\0", 1);
				if (ret < 0) {
					ux_err("Can't write 0 to %s\n", dest_table);
					goto out;
				}
				SHA256Update(&chain_ctxs[chain_idx], (const uint8_t *)"\0", 1);
			}
			chain_idx++;
		}
	}

	/* Step 3: Hash and append backwards.  Each file's final content is the
	 * digest payload written in Step 2 plus the hash of the next file appended
	 * in the previous iteration.  Finalize a copy of the cached context (updated
	 * with the appended hash where applicable) instead of re-reading the file.
	 */
	for (ssize_t i = chained_num - 1; i >= 0; --i) {
		SHA2_CTX ctx = chain_ctxs[i];

		if (i < (ssize_t)chained_num - 1)
			SHA256Update(&ctx, hash, SHA256_DIGEST_LENGTH);

		SHA256Final(hash, &ctx);

		if (i == 0) {
			snprintf(dest_table, sizeof(dest_table), "%s/%s",
				 vip_gen->path, DIGEST_TABLE_TO_SIGN_FILE);
		} else {
			snprintf(dest_table, sizeof(dest_table),
				 "%s/%s%zd.bin", vip_gen->path,
				 CHAINED_TABLE_FILE_PREF, (i - 1));
		}

		ret = write_output_file(dest_table, true, hash, SHA256_DIGEST_LENGTH);
		if (ret < 0) {
			ux_err("Failed to append hash to %s\n", dest_table);
			goto out;
		}
	}

out:
	free(chain_ctxs);
	return ret;
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
	vip_transfer_deinit(qdl);
	return -1;
}

void vip_transfer_deinit(struct qdl_device *qdl)
{
	close(qdl->vip_data.signed_table_fd);
	for (size_t i = 0; i < qdl->vip_data.chained_num; ++i)
		close(qdl->vip_data.chained_fds[i]);
}

static int vip_transfer_send_raw(struct qdl_device *qdl, int table_fd)
{
	struct stat sb;
	int ret;
	void *buf;
	ssize_t n;

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
	if (n < 0 || n != sb.st_size) {
		ux_err("failed to read binary\n");
		ret = -1;
		goto out;
	}

	n = qdl_write(qdl, buf, n, 1000);
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
