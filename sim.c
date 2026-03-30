// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "oscompat.h"
#include "sha2.h"
#include "sim.h"

/*
 * Response XML templates sent by the simulated device.
 *
 * The configure response echoes back MaxPayloadSizeToTargetInBytes matching
 * the host's initial value (1 MiB), so firehose_try_configure() never
 * triggers a second configure exchange.
 */
#define SIM_ACK \
	"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" \
	"<data><response value=\"ACK\" rawmode=\"false\" /></data>"

#define SIM_ACK_RAWMODE \
	"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" \
	"<data><response value=\"ACK\" rawmode=\"true\" /></data>"

#define SIM_CONFIGURE_ACK \
	"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" \
	"<data><response value=\"ACK\" MemoryName=\"ufs\"" \
	" MinVersionSupported=\"1\" Version=\"1\"" \
	" MaxPayloadSizeToTargetInBytes=\"1048576\"" \
	" MaxPayloadSizeToTargetInBytesSupported=\"1048576\"" \
	" MaxXMLSizeInBytes=\"4096\" /></data>"

/* sim_state tracks whether the sim is in XML command mode or raw binary mode */
enum sim_state {
	SIM_STATE_XML,     /* normal state: dequeue queued XML responses */
	SIM_STATE_RAW_IN,  /* receiving raw binary data from host (program) */
	SIM_STATE_RAW_OUT, /* sending raw binary data to host (read) */
};

struct sim_response {
	char *data;
	size_t len;
	struct sim_response *next;
};

/*
 * VIP hash validation state.
 *
 * sim_vip_recv_table() extracts the data-chunk SHA256 hashes from each
 * received VIP table (signed MBN or raw chained binary) and stores them in
 * vip_hashes[]. sim_vip_check_chunk() computes SHA256 of each incoming chunk
 * and compares it against the next stored entry.
 *
 * Maximum capacity:
 *   signed table: MAX_DIGESTS_PER_SIGNED_FILE - 1 = 53 chunk hashes
 *   chained tables: MAX_CHAINED_FILES x (MAX_DIGESTS_PER_CHAINED_FILE - 1)
 *                   = 32 x 255 = 8160 chunk hashes
 *   total: 8213 entries x SHA256_DIGEST_LENGTH bytes = ~256 KiB
 *   (heap-allocated together with the enclosing struct qdl_device_sim)
 */
#define SIM_VIP_MAX_HASHES \
	((MAX_DIGESTS_PER_SIGNED_FILE - 1) + \
	 MAX_CHAINED_FILES * (MAX_DIGESTS_PER_CHAINED_FILE - 1))

struct qdl_device_sim {
	struct qdl_device base;
	struct vip_table_generator *vip_gen;
	bool create_digests;

	/* Response queue consumed by sim_read() */
	struct sim_response *resp_head;
	struct sim_response *resp_tail;

	enum sim_state state;
	size_t raw_remaining; /* bytes of raw data left to transfer */
	bool closed;          /* set after power command to terminate reads fast */

	/* VIP hash validation */
	uint8_t vip_hashes[SIM_VIP_MAX_HASHES][SHA256_DIGEST_LENGTH];
	size_t  vip_hash_count; /* entries populated from received VIP tables */
	size_t  vip_hash_idx;   /* index of the next hash to verify */

	/* Chain hash linking each VIP table to the next */
	uint8_t vip_chain_hash[SHA256_DIGEST_LENGTH];
	bool    vip_has_chain_hash;
};

static void sim_enqueue(struct qdl_device_sim *qdl_sim, const char *xml)
{
	struct sim_response *resp;

	resp = malloc(sizeof(*resp));
	if (!resp)
		err(1, "sim: failed to allocate response entry");

	resp->data = strdup(xml);
	if (!resp->data)
		err(1, "sim: failed to duplicate response string");

	resp->len = strlen(xml);
	resp->next = NULL;

	if (qdl_sim->resp_tail)
		qdl_sim->resp_tail->next = resp;
	else
		qdl_sim->resp_head = resp;

	qdl_sim->resp_tail = resp;
}

static void sim_enqueue_log(struct qdl_device_sim *qdl_sim, const char *handler)
{
	char buf[256];

	snprintf(buf, sizeof(buf),
		 "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>"
		 "<data><log value=\"INFO: Calling handler for %s\" /></data>",
		 handler);
	sim_enqueue(qdl_sim, buf);
}

static unsigned int sim_get_uint_attr(xmlNode *node, const char *name)
{
	xmlChar *val;
	unsigned int ret;

	val = xmlGetProp(node, (xmlChar *)name);
	if (!val)
		return 0;

	ret = (unsigned int)strtoul((char *)val, NULL, 10);
	xmlFree(val);
	return ret;
}

/*
 * Qualcomm MBN header structures (little-endian, packed).
 *
 * The signed VIP table (DigestsToSign.bin.mbn) uses one of these layouts.
 * V5 and V6 share the same image_size/code_size offsets; V7 reorders the
 * fields. Only V5 and V6 are supported for VIP table parsing today.
 */
struct mbn_header_v5 {
	uint32_t image_id;
	uint32_t header_version;
	uint32_t qti_signature_size;
	uint32_t qti_cert_chain_size;
	uint32_t image_size;
	uint32_t code_size;
	uint32_t signature_ptr;
	uint32_t signature_size;
	uint32_t cert_chain_ptr;
	uint32_t cert_chain_size;
} __attribute__((packed));

struct mbn_header_v6 {
	uint32_t image_id;
	uint32_t header_version;
	uint32_t qti_signature_size;
	uint32_t qti_cert_chain_size;
	uint32_t image_size;
	uint32_t code_size;
	uint32_t signature_ptr;
	uint32_t signature_size;
	uint32_t cert_chain_ptr;
	uint32_t cert_chain_size;
	uint32_t qti_metadata_size;
	uint32_t metadata_size;
} __attribute__((packed));

#define MBN_HEADER_VERSION_5	5
#define MBN_HEADER_VERSION_6	6

/*
 * sim_vip_signed_payload() - locate the hash payload in a signed VIP MBN table
 *
 * DigestsToSign.bin.mbn is a Qualcomm MBN image with a packed header followed
 * by the hash data, a signature, and certificate chains. The header_version
 * field identifies the layout; V5 and V6 share the same image_size/code_size
 * offsets. The payload begins at (file_size - image_size) and spans code_size
 * bytes.
 *
 * Returns a pointer into @buf for the payload, or NULL if not recognised.
 * Sets @out_len to the payload byte count on success.
 */
static const uint8_t *sim_vip_signed_payload(const uint8_t *buf, size_t len,
					      size_t *out_len)
{
	struct mbn_header_v6 hdr;
	size_t offset;

	if (len < sizeof(struct mbn_header_v5))
		return NULL;

	memcpy(&hdr, buf, sizeof(struct mbn_header_v5));

	if (hdr.header_version != MBN_HEADER_VERSION_5 &&
	    hdr.header_version != MBN_HEADER_VERSION_6)
		return NULL;

	if (hdr.code_size == 0 ||
	    hdr.code_size % SHA256_DIGEST_LENGTH != 0 ||
	    hdr.code_size > MAX_DIGESTS_PER_SIGNED_FILE * SHA256_DIGEST_LENGTH ||
	    hdr.image_size < hdr.code_size || hdr.image_size > len)
		return NULL;

	offset = len - hdr.image_size;
	if (offset < sizeof(struct mbn_header_v5) || offset + hdr.code_size > len)
		return NULL;

	*out_len = hdr.code_size;
	return buf + offset;
}

/*
 * sim_vip_recv_table() - parse a received VIP table and store chunk hashes
 *
 * @is_signed: true -> DigestsToSign.bin.mbn (MBN-wrapped, signed)
 *             false -> ChainedTableOfDigests<n>.bin (raw binary)
 *
 * The last entry of a full signed table and of a non-final chained table is a
 * chain hash (SHA256 of the following table) rather than a data-chunk hash.
 * It is excluded from the stored list.
 *
 * A chained table is "final" when its raw byte count is not a multiple of
 * SHA256_DIGEST_LENGTH: vip_gen_finalize() appends a single zero-padding byte
 * to the final chained table so that it cannot be a USB-512-byte multiple.
 */
static void sim_vip_recv_table(struct qdl_device_sim *qdl_sim,
			       const void *buf, size_t len, bool is_signed)
{
	const uint8_t *hashes;
	size_t count, data_count;
	bool has_chain_hash;

	if (is_signed) {
		size_t payload_len;

		hashes = sim_vip_signed_payload(buf, len, &payload_len);
		if (!hashes) {
			ux_err("sim: VIP signed table: unrecognised format "
			       "(%zu bytes)\n", len);
			return;
		}

		count = payload_len / SHA256_DIGEST_LENGTH;
		/* Full table uses the last slot for the chain hash */
		has_chain_hash = (count == MAX_DIGESTS_PER_SIGNED_FILE);

		/* Save chain hash so the first chained table can be verified */
		if (has_chain_hash) {
			memcpy(qdl_sim->vip_chain_hash,
			       hashes + (count - 1) * SHA256_DIGEST_LENGTH,
			       SHA256_DIGEST_LENGTH);
			qdl_sim->vip_has_chain_hash = true;
		}
	} else {
		/* Verify this table against the chain hash from the previous table */
		if (qdl_sim->vip_has_chain_hash) {
			uint8_t hash[SHA256_DIGEST_LENGTH];
			char got_hex[SHA256_DIGEST_STRING_LENGTH];
			char exp_hex[SHA256_DIGEST_STRING_LENGTH];
			SHA2_CTX ctx;
			size_t i;

			SHA256Init(&ctx);
			SHA256Update(&ctx, buf, len);
			SHA256Final(hash, &ctx);

			if (memcmp(hash, qdl_sim->vip_chain_hash,
				   SHA256_DIGEST_LENGTH) != 0) {
				for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
					sprintf(got_hex + i * 2, "%02x", hash[i]);
					sprintf(exp_hex + i * 2,
						"%02x",
						qdl_sim->vip_chain_hash[i]);
				}
				got_hex[SHA256_DIGEST_STRING_LENGTH - 1] = '\0';
				exp_hex[SHA256_DIGEST_STRING_LENGTH - 1] = '\0';
				ux_err("sim: VIP chained table chain hash mismatch\n"
				       "  computed:  %s\n"
				       "  expected:  %s\n",
				       got_hex, exp_hex);
			} else {
				ux_debug("sim: VIP chained table chain hash OK\n");
			}
			qdl_sim->vip_has_chain_hash = false;
		}

		hashes = buf;
		if (len % SHA256_DIGEST_LENGTH != 0) {
			/*
			 * Trailing zero-padding byte -> final chained table.
			 * All floor(len / 32) entries are chunk hashes.
			 */
			count = len / SHA256_DIGEST_LENGTH;
			has_chain_hash = false;
		} else {
			/*
			 * Size is a multiple of 32 -> non-final chained table.
			 * The last entry is a chain hash to the next table.
			 */
			count = len / SHA256_DIGEST_LENGTH;
			has_chain_hash = (count == MAX_DIGESTS_PER_CHAINED_FILE);
		}

		/* Save chain hash so the next chained table can be verified */
		if (has_chain_hash) {
			memcpy(qdl_sim->vip_chain_hash,
			       hashes + (count - 1) * SHA256_DIGEST_LENGTH,
			       SHA256_DIGEST_LENGTH);
			qdl_sim->vip_has_chain_hash = true;
		}
	}

	data_count = has_chain_hash ? count - 1 : count;

	if (qdl_sim->vip_hash_count + data_count > SIM_VIP_MAX_HASHES) {
		ux_err("sim: VIP table overflow (%zu + %zu > %u), "
		       "truncating\n",
		       qdl_sim->vip_hash_count, data_count, SIM_VIP_MAX_HASHES);
		data_count = SIM_VIP_MAX_HASHES - qdl_sim->vip_hash_count;
	}

	memcpy(qdl_sim->vip_hashes[qdl_sim->vip_hash_count],
	       hashes, data_count * SHA256_DIGEST_LENGTH);
	qdl_sim->vip_hash_count += data_count;

	ux_debug("sim: loaded %zu VIP chunk hashes from %s table "
		 "(total: %zu)\n",
		 data_count, is_signed ? "signed" : "chained",
		 qdl_sim->vip_hash_count);
}

/*
 * sim_vip_check_chunk() - SHA256 @buf/@len and verify against the next entry
 *
 * Called once per VIP chunk (one XML command write or one raw-data write).
 * A mismatch is reported via ux_err() but does not abort, so that all
 * mismatches across a full flash run are visible at once.
 */
static void sim_vip_check_chunk(struct qdl_device_sim *qdl_sim,
				const void *buf, size_t len)
{
	uint8_t hash[SHA256_DIGEST_LENGTH];
	char got_hex[SHA256_DIGEST_STRING_LENGTH];
	char exp_hex[SHA256_DIGEST_STRING_LENGTH];
	const uint8_t *expected;
	SHA2_CTX ctx;
	size_t i;

	if (qdl_sim->create_digests || !qdl_sim->vip_hash_count)
		return;

	SHA256Init(&ctx);
	SHA256Update(&ctx, buf, len);
	SHA256Final(hash, &ctx);

	if (qdl_sim->vip_hash_idx >= qdl_sim->vip_hash_count) {
		ux_err("sim: VIP chunk %zu has no entry in the digest table "
		       "(table has %zu entries)\n",
		       qdl_sim->vip_hash_idx, qdl_sim->vip_hash_count);
		qdl_sim->vip_hash_idx++;
		return;
	}

	expected = qdl_sim->vip_hashes[qdl_sim->vip_hash_idx];

	if (memcmp(hash, expected, SHA256_DIGEST_LENGTH) == 0) {
		ux_debug("sim: VIP chunk %zu hash OK\n",
			 qdl_sim->vip_hash_idx);
		qdl_sim->vip_hash_idx++;
		return;
	}

	for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
		sprintf(got_hex + i * 2, "%02x", hash[i]);
		sprintf(exp_hex + i * 2, "%02x", expected[i]);
	}
	got_hex[SHA256_DIGEST_STRING_LENGTH - 1] = '\0';
	exp_hex[SHA256_DIGEST_STRING_LENGTH - 1] = '\0';

	ux_err("sim: VIP hash mismatch for chunk %zu\n"
	       "  computed:  %s\n"
	       "  expected:  %s\n",
	       qdl_sim->vip_hash_idx, got_hex, exp_hex);

	qdl_sim->vip_hash_idx++;
}

static int sim_open(struct qdl_device *qdl __unused,
		    const char *serial __unused)
{
	ux_info("This is a dry-run execution of QDL. No actual flashing has been performed\n");
	return 0;
}

static void sim_close(struct qdl_device *qdl __unused) {}

/*
 * sim_read() - serve the next queued XML response or raw-out data
 *
 * Returns the number of bytes written to @buf, or a negative errno.
 * Returns -ETIMEDOUT when the response queue is empty (mirroring the
 * behaviour of the USB driver when no data arrives within the timeout).
 * Returns -EIO after a power/reset command to let the caller drain quickly.
 */
static int sim_read(struct qdl_device *qdl, void *buf, size_t len,
		    unsigned int timeout __unused)
{
	struct qdl_device_sim *qdl_sim = container_of(qdl, struct qdl_device_sim, base);
	struct sim_response *resp;
	size_t copy_len;

	/*
	 * Always drain the response queue first. This matters for read
	 * commands: sim_write() sets SIM_STATE_RAW_OUT and queues the
	 * rawmode=true ACK at the same time, so the pending XML must be
	 * served before we start sending raw sector data.
	 */
	resp = qdl_sim->resp_head;
	if (resp) {
		copy_len = resp->len;
		memcpy(buf, resp->data, copy_len);
		qdl_sim->resp_head = resp->next;
		if (!qdl_sim->resp_head)
			qdl_sim->resp_tail = NULL;
		free(resp->data);
		free(resp);
		return copy_len;
	}

	/*
	 * Queue empty and in raw-out mode: the host has received the
	 * rawmode=true ACK and is now reading sector data. Serve zeros
	 * and enqueue the final rawmode=false ACK when all sectors are done.
	 */
	if (qdl_sim->state == SIM_STATE_RAW_OUT) {
		copy_len = MIN(len, qdl_sim->raw_remaining);
		memset(buf, 0, copy_len);
		qdl_sim->raw_remaining -= copy_len;
		if (qdl_sim->raw_remaining == 0) {
			sim_enqueue(qdl_sim, SIM_ACK);
			qdl_sim->state = SIM_STATE_XML;
		}
		return copy_len;
	}

	/*
	 * Queue is empty. Return -EIO after power/reset so that callers
	 * which drain trailing log messages after the final ACK return
	 * immediately instead of spinning until the timeout expires.
	 */
	if (qdl_sim->closed)
		return -EIO;

	return -ETIMEDOUT;
}

/*
 * sim_write() - accept a host write and queue the matching device response(s)
 *
 * In SIM_STATE_RAW_IN the simulator counts down the expected raw payload and
 * queues the final rawmode=false ACK once the last byte arrives.
 *
 * In SIM_STATE_XML the XML command is parsed and one or more response messages
 * are pushed onto the queue for sim_read() to serve back.
 *
 * In dry-run mode without digest generation, binary writes that fail XML
 * parsing are treated as VIP digest tables: sim_vip_recv_table() extracts the
 * chunk hashes and an ACK is enqueued so that the host's post-send
 * firehose_read() succeeds. Every subsequent XML command write and every
 * raw-data write is then verified against the stored hashes by
 * sim_vip_check_chunk().
 */
static int sim_write(struct qdl_device *qdl, const void *buf, size_t len,
		     unsigned int timeout __unused)
{
	struct qdl_device_sim *qdl_sim = container_of(qdl, struct qdl_device_sim, base);
	unsigned int num_sectors, sector_size;
	xmlNode *root, *node, *child;
	xmlDoc *doc;

	/*
	 * VIP table writes (both the signed MBN and chained raw binaries) may
	 * arrive while SIM_STATE_RAW_IN is active, interleaved between data
	 * chunks of the same program operation. Intercept them here using the
	 * sending_table flag set exclusively by vip_transfer_send_raw(), before
	 * the raw-data accounting branch below.
	 */
	if (!qdl_sim->create_digests &&
	    qdl_sim->base.vip_data.sending_table) {
		bool is_signed = (qdl_sim->base.vip_data.state == VIP_INIT);

		sim_vip_recv_table(qdl_sim, buf, len, is_signed);
		sim_enqueue(qdl_sim, SIM_ACK);
		return len;
	}

	/* Raw binary payload for an ongoing program operation */
	if (qdl_sim->state == SIM_STATE_RAW_IN) {
		/*
		 * Each qdl_write() call maps to exactly one VIP chunk
		 * (firehose_do_program() loops over max_payload_size chunks,
		 * calling vip_gen_chunk_init/update/store once per iteration).
		 * Hash the entire write and verify against the digest table.
		 */
		sim_vip_check_chunk(qdl_sim, buf, len);

		if (len >= qdl_sim->raw_remaining) {
			sim_enqueue(qdl_sim, SIM_ACK);
			qdl_sim->state = SIM_STATE_XML;
			qdl_sim->raw_remaining = 0;
		} else {
			qdl_sim->raw_remaining -= len;
		}
		return len;
	}

	/* XML command: parse and enqueue appropriate response(s) */
	doc = xmlReadMemory(buf, len, NULL, NULL,
			    XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
	if (!doc)
		return len;

	/*
	 * Valid XML command: verify it against the digest table before
	 * dispatching so that the hash index stays in step with the
	 * vip_gen_chunk_store() calls made during the dry-run that
	 * produced the table.
	 */
	sim_vip_check_chunk(qdl_sim, buf, len);

	root = xmlDocGetRootElement(doc);

	/* Locate the <data> element */
	for (node = root; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE &&
		    xmlStrcmp(node->name, (xmlChar *)"data") == 0)
			break;
	}
	if (!node)
		goto out;

	/* First child element is the command */
	for (child = node->children;
	     child && child->type != XML_ELEMENT_NODE;
	     child = child->next)
		;
	if (!child)
		goto out;

	if (xmlStrcmp(child->name, (xmlChar *)"configure") == 0) {
		sim_enqueue_log(qdl_sim, "configure");
		sim_enqueue(qdl_sim, SIM_CONFIGURE_ACK);

	} else if (xmlStrcmp(child->name, (xmlChar *)"program") == 0) {
		num_sectors = sim_get_uint_attr(child, "num_partition_sectors");
		sector_size = sim_get_uint_attr(child, "SECTOR_SIZE_IN_BYTES");
		sim_enqueue_log(qdl_sim, "program");
		sim_enqueue(qdl_sim, SIM_ACK_RAWMODE);
		qdl_sim->state = SIM_STATE_RAW_IN;
		qdl_sim->raw_remaining = (size_t)num_sectors * sector_size;

	} else if (xmlStrcmp(child->name, (xmlChar *)"read") == 0) {
		num_sectors = sim_get_uint_attr(child, "num_partition_sectors");
		sector_size = sim_get_uint_attr(child, "SECTOR_SIZE_IN_BYTES");
		sim_enqueue_log(qdl_sim, "read");
		sim_enqueue(qdl_sim, SIM_ACK_RAWMODE);
		qdl_sim->state = SIM_STATE_RAW_OUT;
		qdl_sim->raw_remaining = (size_t)num_sectors * sector_size;

	} else if (xmlStrcmp(child->name, (xmlChar *)"erase") == 0) {
		sim_enqueue_log(qdl_sim, "erase");
		sim_enqueue(qdl_sim, SIM_ACK);

	} else if (xmlStrcmp(child->name, (xmlChar *)"patch") == 0) {
		sim_enqueue_log(qdl_sim, "patch");
		sim_enqueue(qdl_sim, SIM_ACK);

	} else if (xmlStrcmp(child->name, (xmlChar *)"setbootablestoragedrive") == 0) {
		sim_enqueue_log(qdl_sim, "setbootablestoragedrive");
		sim_enqueue(qdl_sim, SIM_ACK);

	} else if (xmlStrcmp(child->name, (xmlChar *)"power") == 0) {
		sim_enqueue_log(qdl_sim, "power");
		sim_enqueue(qdl_sim, SIM_ACK);
		/*
		 * Mark the sim closed so that the trailing drain read in
		 * firehose_reset() returns -EIO immediately rather than
		 * spinning until the timeout expires.
		 */
		qdl_sim->closed = true;

	} else if (xmlStrcmp(child->name, (xmlChar *)"ufs") == 0) {
		sim_enqueue_log(qdl_sim, "ufs");
		sim_enqueue(qdl_sim, SIM_ACK);

	} else {
		/* Unknown command: respond with a generic ACK */
		sim_enqueue(qdl_sim, SIM_ACK);
	}

out:
	xmlFreeDoc(doc);
	return len;
}

static void sim_set_out_chunk_size(struct qdl_device *qdl __unused,
				   long size __unused)
{}

struct qdl_device *sim_init(void)
{
	struct qdl_device *qdl = malloc(sizeof(struct qdl_device_sim));

	if (!qdl)
		return NULL;

	memset(qdl, 0, sizeof(struct qdl_device_sim));

	qdl->dev_type = QDL_DEVICE_SIM;
	qdl->open = sim_open;
	qdl->read = sim_read;
	qdl->write = sim_write;
	qdl->close = sim_close;
	qdl->set_out_chunk_size = sim_set_out_chunk_size;
	qdl->max_payload_size = 1048576;
	/*
	 * Pre-set a non-zero sector size so that firehose_try_configure()
	 * skips the sector-size probe reads. Those probe commands would
	 * otherwise be hashed into the VIP digest table during dry-run
	 * digest generation (--createdigest), but they are never sent
	 * during the real VIP flash -- causing a hash mismatch.
	 */
	qdl->sector_size = 4096;

	return qdl;
}

struct vip_table_generator *sim_get_vip_generator(struct qdl_device *qdl)
{
	struct qdl_device_sim *qdl_sim;

	if (qdl->dev_type != QDL_DEVICE_SIM)
		return NULL;

	qdl_sim = container_of(qdl, struct qdl_device_sim, base);

	if (!qdl_sim->create_digests)
		return NULL;

	return qdl_sim->vip_gen;
}

bool sim_set_digest_generation(bool create_digests, struct qdl_device *qdl,
			       struct vip_table_generator *vip_gen)
{
	struct qdl_device_sim *qdl_sim;

	if (qdl->dev_type != QDL_DEVICE_SIM)
		return false;

	qdl_sim = container_of(qdl, struct qdl_device_sim, base);

	qdl_sim->create_digests = create_digests;
	qdl_sim->vip_gen = vip_gen;

	return true;
}
