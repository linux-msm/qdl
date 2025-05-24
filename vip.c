/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sim.h"

#define DIGEST_FULL_TABLE_FILE			"DIGEST_TABLE.bin"
#define CHAINED_TABLE_FILE_PREF			"ChainedTableOfDigests"
#define CHAINED_TABLE_FILE_MAX_NAME		64
#define DIGEST_TABLE_TO_SIGN_FILE		"DigestsToSign.bin"
#define MAX_DIGESTS_PER_SIGNED_FILE		54
#define MAX_DIGESTS_PER_SIGNED_TABLE		(MAX_DIGESTS_PER_SIGNED_FILE - 1)
#define MAX_DIGESTS_PER_CHAINED_FILE		256
#define MAX_DIGESTS_PER_CHAINED_TABLE		(MAX_DIGESTS_PER_CHAINED_FILE - 1)
#define MAX_DIGESTS_PER_BUF			16

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
	struct qdl_device_sim *qdl_sim;
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

	qdl_sim = container_of(qdl, struct qdl_device_sim, base);
	vip_gen = &qdl_sim->vip_gen;

	qdl_sim->create_digests = true;
	vip_gen->digest_num_written = 0;
	vip_gen->path = path;

	snprintf(filepath, sizeof(filepath), "%s/%s", path, DIGEST_FULL_TABLE_FILE);

	vip_gen->digest_table_fd = fopen(filepath, "wb");
	if (!vip_gen->digest_table_fd) {
		ux_err("Can't create %s file\n", filepath);
		return -1;
	}

	return 0;
}

void vip_gen_chunk_init(struct qdl_device *qdl)
{
	struct qdl_device_sim *qdl_sim;
	struct vip_table_generator *vip_gen;

	if (qdl->dev_type != QDL_DEVICE_SIM)
		return;

	qdl_sim = container_of(qdl, struct qdl_device_sim, base);
	vip_gen = &(qdl_sim->vip_gen);

	if (!qdl_sim->create_digests)
		return;

	SHA256Init(&vip_gen->ctx);
}

void vip_gen_chunk_update(struct qdl_device *qdl, const void *buf, size_t len)
{
	struct qdl_device_sim *qdl_sim;
	struct vip_table_generator *vip_gen;

	if (qdl->dev_type != QDL_DEVICE_SIM)
		return;

	qdl_sim = container_of(qdl, struct qdl_device_sim, base);
	vip_gen = &qdl_sim->vip_gen;

	if (!qdl_sim->create_digests)
		return;

	SHA256Update(&vip_gen->ctx, (uint8_t *)buf, len);
}

void vip_gen_chunk_store(struct qdl_device *qdl)
{
	struct qdl_device_sim *qdl_sim;
	struct vip_table_generator *vip_gen;

	if (qdl->dev_type != QDL_DEVICE_SIM)
		return;

	qdl_sim = container_of(qdl, struct qdl_device_sim, base);
	vip_gen = &qdl_sim->vip_gen;

	if (!qdl_sim->create_digests)
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

static int create_chained_tables(struct vip_table_generator *vip_gen)
{
	size_t tosign_count = 0;
	size_t written = 0;
	size_t to_read;
	size_t bytes;
	size_t total_digests = vip_gen->digest_num_written;
	off_t offset;
	char filepath[PATH_MAX];
	char filepath_pr[PATH_MAX];
	size_t chained_num = 0;
	const size_t elem_size = SHA256_DIGEST_LENGTH;
	unsigned char hash[SHA256_DIGEST_LENGTH];
	unsigned char buf[MAX_DIGESTS_PER_BUF * SHA256_DIGEST_LENGTH];
	int ret;

	snprintf(filepath, sizeof(filepath), "%s/%s", vip_gen->path, DIGEST_FULL_TABLE_FILE);
	int fd = open(filepath, O_RDONLY);
	if (fd < 0) {
		ux_err("Failed to open " DIGEST_FULL_TABLE_FILE " for reading\n");
		return -1;
	}

	/* Step 1: Write digest table to DigestsToSign.bin */
	snprintf(filepath_pr, sizeof(filepath_pr), "%s/%s",
		 vip_gen->path, DIGEST_TABLE_TO_SIGN_FILE);
	tosign_count = total_digests < MAX_DIGESTS_PER_SIGNED_TABLE ? total_digests :
		       MAX_DIGESTS_PER_SIGNED_TABLE;

	while (written < (tosign_count * elem_size)) {
		to_read = tosign_count * elem_size - written;
		if (to_read > sizeof(buf))
			to_read = sizeof(buf);

		bytes = read(fd, buf, to_read);
		if (bytes < 0 || (size_t) bytes != to_read) {
			ux_err("Failed to read from " DIGEST_FULL_TABLE_FILE "\n");
			goto out_cleanup;
		}

		ret = write_output_file(filepath_pr, (written != 0), buf, bytes);
		if (ret < 0) {
			ux_err("Can't write digests to %s\n", filepath_pr);
			goto out_cleanup;
		}

		written += to_read;
	}

	/* Step 2: Write remaining digests to ChainedTableOfDigests<n>.bin */
	if (total_digests > MAX_DIGESTS_PER_SIGNED_TABLE) {
		size_t remaining = total_digests - MAX_DIGESTS_PER_SIGNED_TABLE;

		/* Seek to offset after the first MAX_DIGESTS_PER_SIGNED_TABLE digests */
		offset = MAX_DIGESTS_PER_SIGNED_TABLE * elem_size;
		if (lseek(fd, offset, SEEK_SET) != offset) {
			ux_err("Failed to seek in " DIGEST_FULL_TABLE_FILE "\n");
			goto out_cleanup;
		}

		while (remaining > 0) {
			size_t chunk = remaining > MAX_DIGESTS_PER_CHAINED_TABLE ?
						   MAX_DIGESTS_PER_CHAINED_TABLE : remaining;
			size_t chunk_bytes = chunk * elem_size;

			snprintf(filepath, sizeof(filepath),
				 "%s/%s%zu.bin", vip_gen->path,
				 CHAINED_TABLE_FILE_PREF, chained_num);

			written = 0;
			while (written < chunk_bytes) {
				to_read = chunk_bytes - written;
				if (to_read > sizeof(buf))
					to_read = sizeof(buf);

				bytes = read(fd, buf, to_read);
				if (bytes < 0 || (size_t) bytes != to_read) {
					ux_err("Failed to read from "
					       DIGEST_FULL_TABLE_FILE " table\n");
					goto out_cleanup;
				}
				ret = write_output_file(filepath, (written != 0),
							buf, bytes);
				if (ret < 0) {
					ux_err("Can't write digests to %s\n", filepath);
					goto out_cleanup;
				}
				written += to_read;
			}

			remaining -= chunk;
			if (!remaining) {
				/* Add zero (the packet can't be multiple of 512 bytes) */
				ret = write_output_file(filepath, true, "\0", 1);
				if (ret < 0) {
					ux_err("Can't write 0 to %s\n", filepath);
					goto out_cleanup;
				}
			}
			chained_num++;
		}
	}

	/* Step 3: Recursively hash and append backwards */
	for (ssize_t i = chained_num - 1; i >= 0; --i) {
		snprintf(filepath, sizeof(filepath),
			 "%s/%s%zd.bin", vip_gen->path,
			 CHAINED_TABLE_FILE_PREF, i);
		ret = calculate_hash_of_file(filepath, hash);
		if (ret < 0) {
			ux_err("Failed to hash %s\n", filepath);
			goto out_cleanup;
		}

		if (i == 0) {
			snprintf(filepath_pr, sizeof(filepath_pr), "%s/%s",
				 vip_gen->path, DIGEST_TABLE_TO_SIGN_FILE);
			ret = write_output_file(filepath_pr, true, hash, elem_size);
			if (ret < 0) {
				ux_err("Failed to append hash to %s\n", filepath_pr);
				goto out_cleanup;
			}
		} else {
			snprintf(filepath_pr, sizeof(filepath_pr),
				 "%s/%s%zd.bin", vip_gen->path,
				 CHAINED_TABLE_FILE_PREF, (i - 1));
			ret = write_output_file(filepath_pr, true, hash, elem_size);
			if (ret < 0) {
				ux_err("Failed to append hash to %s\n", filepath_pr);
				goto out_cleanup;
			}
		}
	}

	close(fd);

	return 0;

out_cleanup:
	close(fd);

	return -1;
}

void vip_gen_finalize(struct qdl_device *qdl)
{
	struct qdl_device_sim *qdl_sim;
	struct vip_table_generator *vip_gen;

	if (qdl->dev_type != QDL_DEVICE_SIM)
		return;

	qdl_sim = container_of(qdl, struct qdl_device_sim, base);
	vip_gen = &qdl_sim->vip_gen;

	if (!qdl_sim->create_digests)
		return;

	fclose(vip_gen->digest_table_fd);

	ux_debug("VIP TABLE DIGESTS: %lu\n", vip_gen->digest_num_written);

	if (create_chained_tables(vip_gen) < 0)
		ux_err("Error occured when creating table of digests\n");
}