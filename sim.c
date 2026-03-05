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
	 * Always drain the response queue first.  This matters for read
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
	 * rawmode=true ACK and is now reading sector data.  Serve zeros
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
	 * Queue is empty.  Return -EIO after power/reset so that callers
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
 */
static int sim_write(struct qdl_device *qdl, const void *buf, size_t len,
		     unsigned int timeout __unused)
{
	struct qdl_device_sim *qdl_sim = container_of(qdl, struct qdl_device_sim, base);
	unsigned int num_sectors, sector_size;
	xmlNode *root, *node, *child;
	xmlDoc *doc;

	/* Raw binary payload for an ongoing program operation */
	if (qdl_sim->state == SIM_STATE_RAW_IN) {
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
	 * skips the sector-size probe reads.  Those probe commands would
	 * otherwise be hashed into the VIP digest table during dry-run
	 * digest generation (--createdigest), but they are never sent
	 * during the real VIP flash — causing a hash mismatch.
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
