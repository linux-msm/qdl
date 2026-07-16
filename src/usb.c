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
 *   usb_open_once()   - one enumeration pass over the bus: counts
 *                       visible EDL devices, tries to open each
 *   usb_open_device() - matches and opens a single candidate device,
 *                       claiming its EDL interface
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

static bool usb_match_usb_serial(struct libusb_device_handle *handle, const char *serial,
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

	/* bInterfaceProtocol of 0xff, 0x10 and 0x11 has been seen */
	if (ifc->bInterfaceProtocol != 0xff &&
	    ifc->bInterfaceProtocol != 16 &&
	    ifc->bInterfaceProtocol != 17)
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

static int usb_open_device(libusb_device *dev, struct qdl_device_usb *qdl, const char *serial)
{
	const struct libusb_interface_descriptor *ifc;
	struct libusb_config_descriptor *config;
	struct libusb_device_descriptor desc;
	struct libusb_device_handle *handle;
	struct usb_edl_interface edl;
	int ret;
	int k;

	ret = libusb_get_device_descriptor(dev, &desc);
	if (ret < 0) {
		warnx("failed to get USB device descriptor");
		return -1;
	}

	if (!usb_is_edl_device(&desc))
		return 0;

	ret = libusb_get_active_config_descriptor(dev, &config);
	if (ret < 0) {
		warnx("failed to acquire USB device's active config descriptor");
		return -1;
	}

	for (k = 0; k < config->bNumInterfaces; k++) {
		ifc = config->interface[k].altsetting;

		if (!usb_match_edl_interface(ifc, &edl))
			continue;

		ret = libusb_open(dev, &handle);
		if (ret < 0) {
			warnx("unable to open USB device");
			continue;
		}

		if (!usb_match_usb_serial(handle, serial, &desc)) {
			libusb_close(handle);
			continue;
		}

		libusb_detach_kernel_driver(handle, ifc->bInterfaceNumber);

		ret = libusb_claim_interface(handle, ifc->bInterfaceNumber);
		if (ret < 0) {
			warnx("failed to claim USB interface");
			libusb_close(handle);
			continue;
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

		break;
	}

	libusb_free_config_descriptor(config);

	return !!qdl->usb_handle;
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
int usb_open_once(struct qdl_device *qdl, const char *serial, int *visible_out)
{
	struct qdl_device_usb *qdl_usb = container_of(qdl, struct qdl_device_usb, base);
	struct libusb_device_descriptor desc;
	struct libusb_device **devs;
	struct libusb_device *dev;
	char matched_serial[64];
	uint16_t matched_pid = 0;
	bool found = false;
	int visible = 0;
	ssize_t n;
	int ret;
	int i;

	if (visible_out)
		*visible_out = 0;

	ret = libusb_init(NULL);
	if (ret < 0) {
		ux_err("failed to initialize libusb: %s\n", libusb_strerror(ret));
		return -EIO;
	}

	n = libusb_get_device_list(NULL, &devs);
	if (n < 0) {
		ux_err("failed to list USB devices: %s\n", libusb_strerror(n));
		libusb_exit(NULL);
		return -EIO;
	}

	for (i = 0; devs[i]; i++) {
		dev = devs[i];

		if (libusb_get_device_descriptor(dev, &desc) < 0)
			continue;
		if (!usb_is_edl_device(&desc))
			continue;

		visible++;

		ret = usb_open_device(dev, qdl_usb, serial);
		if (ret == 1) {
			found = true;
			matched_pid = desc.idProduct;
			if (!usb_read_serial(qdl_usb->usb_handle, &desc,
					     matched_serial, sizeof(matched_serial)))
				matched_serial[0] = '\0';
			break;
		}
	}

	if (visible_out)
		*visible_out = visible;

	if (found) {
		const char *action = matched_pid == 0x900e ? "Collecting crash dump from" : "Talking to";

		libusb_free_device_list(devs, 1);
		if (matched_serial[0])
			ux_info("%s device (PID 0x%04x, serial: %s)\n",
				action, matched_pid, matched_serial);
		else
			ux_info("%s device (PID 0x%04x)\n", action, matched_pid);
		return 0;
	}

	libusb_free_device_list(devs, 1);
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

struct qdl_device_desc *usb_list(unsigned int *devices_found)
{
	struct libusb_device_descriptor desc;
	struct libusb_device_handle *handle;
	struct qdl_device_desc *result;
	struct libusb_device **devices;
	struct libusb_device *dev;
	unsigned long serial_len;
	unsigned char buf[128];
	ssize_t device_count;
	unsigned int count = 0;
	char *serial;
	int ret;
	int i;

	ret = libusb_init(NULL);
	if (ret < 0) {
		ux_err("failed to initialize libusb: %s\n", libusb_strerror(ret));
		return NULL;
	}

	device_count = libusb_get_device_list(NULL, &devices);
	if (device_count < 0) {
		ux_err("failed to list USB devices: %s\n", libusb_strerror(device_count));
		libusb_exit(NULL);
		return NULL;
	}
	if (device_count == 0) {
		libusb_free_device_list(devices, 1);
		libusb_exit(NULL);
		return NULL;
	}

	result = calloc(device_count, sizeof(struct qdl_device_desc));
	if (!result) {
		ux_err("failed to allocate devices array\n");
		libusb_free_device_list(devices, 1);
		libusb_exit(NULL);
		return NULL;
	}

	for (i = 0; i < device_count; i++) {
		dev = devices[i];

		ret = libusb_get_device_descriptor(dev, &desc);
		if (ret < 0) {
			warnx("failed to get USB device descriptor");
			continue;
		}

		if (!usb_is_edl_device(&desc))
			continue;

		ret = libusb_open(dev, &handle);
		if (ret < 0)
			continue;

		ret = libusb_get_string_descriptor_ascii(handle, desc.iProduct, buf, sizeof(buf) - 1);
		if (ret < 0) {
			warnx("failed to read iProduct descriptor: %s", libusb_strerror(ret));
			libusb_close(handle);
			continue;
		}
		buf[ret] = '\0';

		serial = strstr((char *)buf, "_SN:");
		if (!serial) {
			memcpy(result[count].serial, "(none)", sizeof("(none)"));
		} else {
			serial += strlen("_SN:");
			serial_len = strcspn(serial, " _");
			if (serial_len + 1 > sizeof(result[count].serial)) {
				ux_err("ignoring device with unexpectedly long serial number\n");
				libusb_close(handle);
				continue;
			}

			memcpy(result[count].serial, serial, serial_len);
			result[count].serial[serial_len] = '\0';
		}

		result[count].vid = desc.idVendor;
		result[count].pid = desc.idProduct;
		libusb_close(handle);
		count++;
	}

	libusb_free_device_list(devices, 1);
	libusb_exit(NULL);
	*devices_found = count;

	return result;
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
