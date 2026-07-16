// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * QDL_DEVICE_AUTO: meta-backend that defers transport selection to the
 * wait loop. Each 250 ms tick it runs both the libusb open attempt and
 * (on Windows) a QUD SetupAPI probe; whichever first reaches an EDL
 * device wins, and its concrete qdl_device is bound as the inner. All
 * subsequent qdl_read/write/close calls on the outer forward to the
 * inner.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qdl.h"

struct qdl_device_auto {
	struct qdl_device base;
	struct qdl_device *inner;
	long pending_chunk_size;
	bool chunk_size_set;
};

static struct qdl_device_auto *to_auto(struct qdl_device *qdl)
{
	return container_of(qdl, struct qdl_device_auto, base);
}

static void auto_bind_inner(struct qdl_device_auto *wrap, struct qdl_device *inner)
{
	wrap->inner = inner;
	wrap->base.max_payload_size = inner->max_payload_size;
	if (wrap->chunk_size_set)
		inner->set_out_chunk_size(inner, wrap->pending_chunk_size);
}

static int auto_open(struct qdl_device *qdl, const char *serial)
{
	struct qdl_device_auto *wrap = to_auto(qdl);
	struct qdl_device *usb_dev;
#ifdef _WIN32
	struct qdl_device *qud_dev;
	int qud_count;
#endif
	int visible_prev = -1;
	int visible;
	int ret;

	usb_dev = usb_init();
	if (!usb_dev)
		return -1;

#ifdef _WIN32
	qud_dev = qud_init();
	if (!qud_dev) {
		qdl_deinit(usb_dev);
		return -1;
	}
#endif

	for (;;) {
		ret = usb_open_once(usb_dev, serial, &visible);
		if (ret == 0) {
#ifdef _WIN32
			qdl_deinit(qud_dev);
#endif
			auto_bind_inner(wrap, usb_dev);
			return 0;
		}
		if (ret == -EIO)
			goto fail;

#ifdef _WIN32
		qud_count = qud_probe_present();
		if (qud_count > 0 || ret == -EBUSY) {
			if (qud_dev->open(qud_dev, serial) == 0) {
				qdl_deinit(usb_dev);
				auto_bind_inner(wrap, qud_dev);
				return 0;
			}
		}
		visible += qud_count;
#endif

		if (visible != visible_prev) {
			if (visible == 0)
				ux_info("Waiting for EDL device\n");
			else if (serial)
				ux_info("%d EDL device(s) visible, none match serial \"%s\"\n",
					visible, serial);
			else
				ux_info("%d EDL device(s) visible, none could be opened\n",
					visible);
			visible_prev = visible;
		}

		usleep(250000);
	}

fail:
	qdl_deinit(usb_dev);
#ifdef _WIN32
	qdl_deinit(qud_dev);
#endif
	return -1;
}

static int auto_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout)
{
	struct qdl_device *inner = to_auto(qdl)->inner;

	return inner->read(inner, buf, len, timeout);
}

static int auto_write(struct qdl_device *qdl, const void *buf, size_t len, unsigned int timeout)
{
	struct qdl_device *inner = to_auto(qdl)->inner;

	return inner->write(inner, buf, len, timeout);
}

static void auto_close(struct qdl_device *qdl)
{
	struct qdl_device_auto *wrap = to_auto(qdl);

	if (!wrap->inner)
		return;
	wrap->inner->close(wrap->inner);
	qdl_deinit(wrap->inner);
	wrap->inner = NULL;
}

static void auto_set_out_chunk_size(struct qdl_device *qdl, long size)
{
	struct qdl_device_auto *wrap = to_auto(qdl);

	if (wrap->inner) {
		wrap->inner->set_out_chunk_size(wrap->inner, size);
		return;
	}
	wrap->pending_chunk_size = size;
	wrap->chunk_size_set = true;
}

struct qdl_device *auto_init(void)
{
	struct qdl_device_auto *wrap = calloc(1, sizeof(*wrap));

	if (!wrap)
		return NULL;

	wrap->base.dev_type = QDL_DEVICE_AUTO;
	wrap->base.open = auto_open;
	wrap->base.read = auto_read;
	wrap->base.write = auto_write;
	wrap->base.close = auto_close;
	wrap->base.set_out_chunk_size = auto_set_out_chunk_size;
	wrap->base.max_payload_size = 1048576;

	return &wrap->base;
}
