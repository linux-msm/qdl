// SPDX-License-Identifier: BSD-3-Clause
/*
 * libusb transport backend.
 *
 * Open-path layering, top to bottom:
 *
 *   auto_open()       - meta-backend (auto.c): picks the transport by
 *                       alternating usb_open_once() with (on Windows)
 *                       QUD probes until one reaches an EDL device
 *   usb_open()        - this backend's .open op: wait loop retrying
 *                       usb_open_once() every 250 ms
 *   usb_open_once()   - one enumeration pass over the bus: selects and
 *                       counts the visible EDL devices, tries to open
 *                       each
 *   usb_open_device() - opens one already-selected candidate, claiming
 *                       its EDL interface
 *
 * Candidate selection itself (enumeration plus the EDL identity
 * policy) lives in usb_enumerate_edl_devices(), shared between the
 * open path above and usb_list(), which reads descriptors without
 * claiming anything.
 */
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libusb.h>
#include "oscompat.h"

#include "qdl.h"

#define DEFAULT_OUT_CHUNK_SIZE (1024 * 1024)

struct qdl_device_usb {
	struct qdl_device base;
	struct libusb_device_handle *usb_handle;

	int in_ep;
	int out_ep;

	size_t in_maxpktsize;
	size_t out_maxpktsize;
	size_t out_chunk_size;
};

/* libusb-typed wrapper around the shared EDL identity check */
static bool usb_is_edl_device(const struct libusb_device_descriptor *desc)
{
	return qdl_is_edl_device(desc->idVendor, desc->idProduct);
}

typedef int (*usb_edl_device_cb_t)(struct libusb_device *dev,
				   const struct libusb_device_descriptor *desc,
				   void *data);

/*
 * Enumerate the bus and hand every EDL candidate to @cb, stopping
 * early if @cb returns nonzero. The single place the EDL identity
 * policy is applied to libusb devices; shared by the open and list
 * paths, which differ only in what they do with a candidate. The
 * caller owns the libusb session.
 *
 * Returns the number of candidates seen up to and including the one
 * @cb stopped at, or -EIO if the bus could not be enumerated.
 */
static int usb_enumerate_edl_devices(usb_edl_device_cb_t cb, void *data)
{
	struct libusb_device_descriptor desc;
	struct libusb_device **devs;
	int visible = 0;
	ssize_t n;
	int i;

	n = libusb_get_device_list(NULL, &devs);
	if (n < 0) {
		ux_err("failed to list USB devices: %s\n", libusb_strerror(n));
		return -EIO;
	}

	for (i = 0; devs[i]; i++) {
		if (libusb_get_device_descriptor(devs[i], &desc) < 0)
			continue;
		if (!usb_is_edl_device(&desc))
			continue;

		visible++;

		if (cb(devs[i], &desc, data))
			break;
	}

	libusb_free_device_list(devs, 1);

	return visible;
}

/*
 * libusb commit f0cce43f882d ("core: Fix definition and use of enum
 * libusb_transfer_type") split transfer type and endpoint transfer types.
 * Provide an alias in order to make the code compile with the old (non-split)
 * definition.
 */
#ifndef LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK
#define LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK LIBUSB_TRANSFER_TYPE_BULK
#endif

static bool usb_read_serial(struct libusb_device_handle *handle,
			    const struct libusb_device_descriptor *desc,
			    char *out, size_t out_len)
{
	char buf[128];
	char *p;
	int ret;

	ret = libusb_get_string_descriptor_ascii(handle, desc->iProduct, (unsigned char *)buf, sizeof(buf));
	if (ret < 0) {
		warnx("failed to read iProduct descriptor: %s", libusb_strerror(ret));
		return false;
	}

	p = strstr(buf, "_SN:");
	if (!p)
		return false;

	p += strlen("_SN:");
	p[strcspn(p, " _")] = '\0';

	snprintf(out, out_len, "%s", p);
	return true;
}

static bool usb_match_serial(struct libusb_device_handle *handle, const char *serial,
			     const struct libusb_device_descriptor *desc)
{
	char buf[64];

	/* If no serial is requested, consider everything a match */
	if (!serial)
		return true;

	if (!usb_read_serial(handle, desc, buf, sizeof(buf)))
		return false;

	return strcmp(buf, serial) == 0;
}

/*
 * The EDL interface of a matched device: the bulk endpoint pair the
 * Sahara/Firehose protocols run over.
 */
struct usb_edl_interface {
	int in_ep;
	int out_ep;
	size_t in_maxpktsize;
	size_t out_maxpktsize;
};

/*
 * Check whether one interface descriptor carries the EDL service and,
 * if so, extract its bulk endpoint pair into @edl.
 */
static bool usb_match_edl_interface(const struct libusb_interface_descriptor *ifc,
				    struct usb_edl_interface *edl)
{
	const struct libusb_endpoint_descriptor *endpoint;
	uint8_t type;
	int l;

	if (ifc->bInterfaceClass != 0xff)
		return false;

	if (ifc->bInterfaceSubClass != 0xff)
		return false;

	/*
	 * Sahara is identified by bInterfaceProtocol 0x10, 0x11, or 0x13
	 * on modern devices, and 0xff on older targets.
	 */
	if (ifc->bInterfaceProtocol != 0xff &&
	    ifc->bInterfaceProtocol != 0x10 &&
	    ifc->bInterfaceProtocol != 0x11 &&
	    ifc->bInterfaceProtocol != 0x13)
		return false;

	edl->in_ep = -1;
	edl->out_ep = -1;
	edl->in_maxpktsize = 0;
	edl->out_maxpktsize = 0;

	for (l = 0; l < ifc->bNumEndpoints; l++) {
		endpoint = &ifc->endpoint[l];

		type = endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
		if (type != LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK)
			continue;

		if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
			edl->in_ep = endpoint->bEndpointAddress;
			edl->in_maxpktsize = endpoint->wMaxPacketSize;
		} else {
			edl->out_ep = endpoint->bEndpointAddress;
			edl->out_maxpktsize = endpoint->wMaxPacketSize;
		}
	}

	return true;
}

/*
 * Device-level match, run on an opened EDL candidate: the unit must
 * carry the serial the user asked for (if any) and expose an EDL
 * interface. On a match, returns the number of the interface to
 * claim and its bulk endpoint pair.
 */
static bool usb_match_device(struct libusb_device_handle *handle,
			     const struct libusb_device_descriptor *desc,
			     const char *serial,
			     int *ifc_num, struct usb_edl_interface *edl)
{
	const struct libusb_interface_descriptor *ifc;
	struct libusb_config_descriptor *config;
	bool found = false;
	int ret;
	int k;

	if (!usb_match_serial(handle, serial, desc))
		return false;

	ret = libusb_get_active_config_descriptor(libusb_get_device(handle), &config);
	if (ret < 0) {
		warnx("failed to acquire USB device's active config descriptor");
		return false;
	}

	for (k = 0; k < config->bNumInterfaces && !found; k++) {
		ifc = config->interface[k].altsetting;

		if (usb_match_edl_interface(ifc, edl)) {
			*ifc_num = ifc->bInterfaceNumber;
			found = true;
		}
	}

	libusb_free_config_descriptor(config);

	return found;
}

static int usb_open_device(libusb_device *dev,
			   const struct libusb_device_descriptor *desc,
			   struct qdl_device_usb *qdl, const char *serial)
{
	struct libusb_device_handle *handle;
	struct usb_edl_interface edl;
	int ifc_num;
	int ret;

	ret = libusb_open(dev, &handle);
	if (ret < 0) {
		warnx("unable to open USB device");
		return 0;
	}

	if (!usb_match_device(handle, desc, serial, &ifc_num, &edl))
		goto close;

	libusb_detach_kernel_driver(handle, ifc_num);

	ret = libusb_claim_interface(handle, ifc_num);
	if (ret < 0) {
		warnx("failed to claim USB interface");
		goto close;
	}

	qdl->usb_handle = handle;
	qdl->in_ep = edl.in_ep;
	qdl->out_ep = edl.out_ep;
	qdl->in_maxpktsize = edl.in_maxpktsize;
	qdl->out_maxpktsize = edl.out_maxpktsize;

	if (qdl->out_chunk_size && qdl->out_chunk_size % edl.out_maxpktsize) {
		ux_err("WARNING: requested out-chunk-size must be multiple of the device's wMaxPacketSize %ld, using %ld\n",
		       edl.out_maxpktsize, edl.out_maxpktsize);
		qdl->out_chunk_size = edl.out_maxpktsize;
	} else if (!qdl->out_chunk_size) {
		qdl->out_chunk_size = DEFAULT_OUT_CHUNK_SIZE;
	}

	ux_debug("USB: using out-chunk-size of %ld\n", qdl->out_chunk_size);

	return 1;

close:
	libusb_close(handle);
	return 0;
}

/*
 * usb_open_once() - one libusb scan-and-open pass.
 *
 * Used as the single iteration unit by both usb_open() (the --backend usb
 * wait loop) and auto_open() (the unified Windows libusb+QUD wait loop).
 *
 * On success, populates @qdl and emits the "Flashing/Collecting device"
 * UX line; the caller need not log anything.
 *
 * Returns:
 *    0          - device opened, @qdl is ready
 *   -ENODEV     - no EDL device currently visible
 *   -EBUSY      - EDL device(s) visible but none could be opened
 *                 (typically: another driver, usually the Qualcomm
 *                  QDLoader 9008 driver behind the QUD backend, has
 *                  claimed the USB interface)
 *   -EIO        - libusb itself failed (init/get_device_list)
 *
 * @visible_out, when non-NULL, receives the number of EDL devices that
 * libusb saw on this pass, so callers that loop can print transition
 * diagnostics without re-enumerating.
 *
 * Does its own libusb_init()/libusb_exit() so the next call sees a
 * fresh enumeration (libusb's udev-less backends otherwise cache the
 * list, which would mask newly-attached devices in containerised setups).
 */
struct usb_open_ctx {
	struct qdl_device_usb *qdl;
	const char *serial;
	char matched_serial[64];
	uint16_t matched_pid;
	bool found;
};

static int usb_open_cb(libusb_device *dev,
		       const struct libusb_device_descriptor *desc, void *data)
{
	struct usb_open_ctx *ctx = data;

	if (usb_open_device(dev, desc, ctx->qdl, ctx->serial) != 1)
		return 0;

	ctx->found = true;
	ctx->matched_pid = desc->idProduct;
	if (!usb_read_serial(ctx->qdl->usb_handle, desc,
			     ctx->matched_serial, sizeof(ctx->matched_serial)))
		ctx->matched_serial[0] = '\0';

	return 1;
}

int usb_open_once(struct qdl_device *qdl, const char *serial, int *visible_out)
{
	struct qdl_device_usb *qdl_usb = container_of(qdl, struct qdl_device_usb, base);
	struct usb_open_ctx ctx = { .qdl = qdl_usb, .serial = serial };
	int visible;
	int ret;

	if (visible_out)
		*visible_out = 0;

	ret = libusb_init(NULL);
	if (ret < 0) {
		ux_err("failed to initialize libusb: %s\n", libusb_strerror(ret));
		return -EIO;
	}

	visible = usb_enumerate_edl_devices(usb_open_cb, &ctx);
	if (visible < 0) {
		libusb_exit(NULL);
		return -EIO;
	}

	if (visible_out)
		*visible_out = visible;

	if (ctx.found) {
		const char *action = ctx.matched_pid == 0x900e ?
				     "Collecting crash dump from" : "Talking to";

		if (ctx.matched_serial[0])
			ux_info("%s device (PID 0x%04x, serial: %s)\n",
				action, ctx.matched_pid, ctx.matched_serial);
		else
			ux_info("%s device (PID 0x%04x)\n", action, ctx.matched_pid);
		return 0;
	}

	libusb_exit(NULL);

	return visible == 0 ? -ENODEV : -EBUSY;
}

static int usb_open(struct qdl_device *qdl, const char *serial)
{
	int visible_prev = -1;
	int visible;
	int ret;

	for (;;) {
		ret = usb_open_once(qdl, serial, &visible);
		if (ret == 0)
			return 0;
		if (ret == -EIO)
			return -1;

		if (visible != visible_prev) {
			if (visible == 0) {
				ux_info("Waiting for EDL device\n");
			} else if (serial) {
				ux_info("%d EDL device(s) visible, none match serial \"%s\"\n",
					visible, serial);
			} else {
				ux_info("%d EDL device(s) visible, none could be opened\n",
					visible);
			}
			visible_prev = visible;
		}

		usleep(250000);
	}
}

struct usb_list_ctx {
	struct qdl_device_desc *result;
	unsigned int capacity;
	unsigned int count;
};

static int usb_list_cb(libusb_device *dev,
		       const struct libusb_device_descriptor *desc, void *data)
{
	struct libusb_device_handle *handle;
	struct usb_list_ctx *ctx = data;
	struct qdl_device_desc *entry;

	if (ctx->count == ctx->capacity) {
		unsigned int capacity = ctx->capacity ? ctx->capacity * 2 : 8;

		entry = realloc(ctx->result, capacity * sizeof(*entry));
		if (!entry) {
			ux_err("failed to allocate devices array\n");
			return 1;
		}
		ctx->result = entry;
		ctx->capacity = capacity;
	}
	entry = &ctx->result[ctx->count];

	if (libusb_open(dev, &handle) < 0)
		return 0;

	if (!usb_read_serial(handle, desc, entry->serial, sizeof(entry->serial)))
		memcpy(entry->serial, "(none)", sizeof("(none)"));

	entry->vid = desc->idVendor;
	entry->pid = desc->idProduct;
	libusb_close(handle);
	ctx->count++;

	return 0;
}

struct qdl_device_desc *usb_list(unsigned int *devices_found)
{
	struct usb_list_ctx ctx = { 0 };
	int ret;

	ret = libusb_init(NULL);
	if (ret < 0) {
		ux_err("failed to initialize libusb: %s\n", libusb_strerror(ret));
		return NULL;
	}

	ret = usb_enumerate_edl_devices(usb_list_cb, &ctx);
	libusb_exit(NULL);

	if (ret < 0) {
		free(ctx.result);
		return NULL;
	}

	*devices_found = ctx.count;

	return ctx.result;
}

static void usb_close(struct qdl_device *qdl)
{
	struct qdl_device_usb *qdl_usb = container_of(qdl, struct qdl_device_usb, base);

	libusb_close(qdl_usb->usb_handle);
	libusb_exit(NULL);
}

static int usb_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout)
{
	struct qdl_device_usb *qdl_usb = container_of(qdl, struct qdl_device_usb, base);
	int actual;
	int ret;

	ret = libusb_bulk_transfer(qdl_usb->usb_handle, qdl_usb->in_ep, buf, len, &actual, timeout);
	if (ret != 0 && ret != LIBUSB_ERROR_TIMEOUT)
		return -EIO;

	if (ret == LIBUSB_ERROR_TIMEOUT && actual == 0)
		return -ETIMEDOUT;

	/* If what we read equals the endpoint's Max Packet Size, consume the ZLP explicitly */
	if (len == (size_t)actual && !(actual % qdl_usb->in_maxpktsize)) {
		ret = libusb_bulk_transfer(qdl_usb->usb_handle, qdl_usb->in_ep,
					   NULL, 0, NULL, timeout);
		if (ret)
			warnx("Unable to read ZLP: %s", libusb_strerror(ret));
	}

	return actual;
}

static int usb_write(struct qdl_device *qdl, const void *buf, size_t len, unsigned int timeout)
{
	unsigned char *data = (unsigned char *)buf;
	struct qdl_device_usb *qdl_usb = container_of(qdl, struct qdl_device_usb, base);
	unsigned int count = 0;
	size_t len_orig = len;
	int actual;
	int xfer;
	int ret;

	while (len > 0) {
		xfer = (len > qdl_usb->out_chunk_size) ? qdl_usb->out_chunk_size : len;

		ret = libusb_bulk_transfer(qdl_usb->usb_handle, qdl_usb->out_ep, data,
					   xfer, &actual, timeout);
		if (ret != 0 && ret != LIBUSB_ERROR_TIMEOUT) {
			warnx("bulk write failed: %s", libusb_strerror(ret));
			return -EIO;
		}
		if (ret == LIBUSB_ERROR_TIMEOUT && actual == 0)
			return -ETIMEDOUT;

		count += actual;
		len -= actual;
		data += actual;
	}

	if (len_orig % qdl_usb->out_maxpktsize == 0) {
		ret = libusb_bulk_transfer(qdl_usb->usb_handle, qdl_usb->out_ep, NULL,
					   0, &actual, timeout);
		if (ret < 0)
			return -EIO;
	}

	return count;
}

static void usb_set_out_chunk_size(struct qdl_device *qdl, long size)
{
	struct qdl_device_usb *qdl_usb = container_of(qdl, struct qdl_device_usb, base);

	qdl_usb->out_chunk_size = size;
}

struct qdl_device *usb_init(void)
{
	struct qdl_device *qdl = malloc(sizeof(struct qdl_device_usb));

	if (!qdl)
		return NULL;

	memset(qdl, 0, sizeof(struct qdl_device_usb));

	qdl->dev_type = QDL_DEVICE_USB;
	qdl->open = usb_open;
	qdl->read = usb_read;
	qdl->write = usb_write;
	qdl->close = usb_close;
	qdl->set_out_chunk_size = usb_set_out_chunk_size;
	qdl->max_payload_size = 1048576;

	return qdl;
}
