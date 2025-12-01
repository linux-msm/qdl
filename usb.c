// SPDX-License-Identifier: BSD-3-Clause
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

/*
 * libusb commit f0cce43f882d ("core: Fix definition and use of enum
 * libusb_transfer_type") split transfer type and endpoint transfer types.
 * Provide an alias in order to make the code compile with the old (non-split)
 * definition.
 */
#ifndef LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK
#define LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK LIBUSB_TRANSFER_TYPE_BULK
#endif

static bool usb_match_usb_serial(struct libusb_device_handle *handle, const char *serial,
				 const struct libusb_device_descriptor *desc)
{
	char buf[128];
	char *p;
	int ret;

	/* If no serial is requested, consider everything a match */
	if (!serial)
		return true;

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

	return strcmp(p, serial) == 0;
}

static int usb_try_open(libusb_device *dev, struct qdl_device_usb *qdl, const char *serial)
{
	const struct libusb_endpoint_descriptor *endpoint;
	const struct libusb_interface_descriptor *ifc;
	struct libusb_config_descriptor *config;
	struct libusb_device_descriptor desc;
	struct libusb_device_handle *handle;
	size_t out_size;
	size_t in_size;
	uint8_t type;
	int ret;
	int out;
	int in;
	int k;
	int l;

	ret = libusb_get_device_descriptor(dev, &desc);
	if (ret < 0) {
		warnx("failed to get USB device descriptor");
		return -1;
	}

	/* Consider only devices with vid 0x0506 and known product id */
	if (desc.idVendor != 0x05c6)
		return 0;
	if (desc.idProduct != 0x9008 && desc.idProduct != 0x900e && desc.idProduct != 0x901d)
		return 0;

	ret = libusb_get_active_config_descriptor(dev, &config);
	if (ret < 0) {
		warnx("failed to acquire USB device's active config descriptor");
		return -1;
	}

	for (k = 0; k < config->bNumInterfaces; k++) {
		ifc = config->interface[k].altsetting;

		in = -1;
		out = -1;
		in_size = 0;
		out_size = 0;

		for (l = 0; l < ifc->bNumEndpoints; l++) {
			endpoint = &ifc->endpoint[l];

			type = endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
			if (type != LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK)
				continue;

			if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
				in = endpoint->bEndpointAddress;
				in_size = endpoint->wMaxPacketSize;
			} else {
				out = endpoint->bEndpointAddress;
				out_size = endpoint->wMaxPacketSize;
			}
		}

		if (ifc->bInterfaceClass != 0xff)
			continue;

		if (ifc->bInterfaceSubClass != 0xff)
			continue;

		/* bInterfaceProtocol of 0xff, 0x10 and 0x11 has been seen */
		if (ifc->bInterfaceProtocol != 0xff &&
		    ifc->bInterfaceProtocol != 16 &&
		    ifc->bInterfaceProtocol != 17)
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
		qdl->in_ep = in;
		qdl->out_ep = out;
		qdl->in_maxpktsize = in_size;
		qdl->out_maxpktsize = out_size;

		if (qdl->out_chunk_size && qdl->out_chunk_size % out_size) {
			ux_err("WARNING: requested out-chunk-size must be multiple of the device's wMaxPacketSize %ld, using %ld\n",
			       out_size, out_size);
			qdl->out_chunk_size = out_size;
		} else if (!qdl->out_chunk_size) {
			qdl->out_chunk_size = DEFAULT_OUT_CHUNK_SIZE;
		}

		ux_debug("USB: using out-chunk-size of %ld\n", qdl->out_chunk_size);

		break;
	}

	libusb_free_config_descriptor(config);

	return !!qdl->usb_handle;
}

static int usb_open(struct qdl_device *qdl, const char *serial)
{
	struct libusb_device **devs;
	struct libusb_device *dev;
	struct qdl_device_usb *qdl_usb = container_of(qdl, struct qdl_device_usb, base);
	bool wait_printed = false;
	bool found = false;
	ssize_t n;
	int ret;
	int i;

	ret = libusb_init(NULL);
	if (ret < 0)
		err(1, "failed to initialize libusb");

	for (;;) {
		n = libusb_get_device_list(NULL, &devs);
		if (n < 0)
			err(1, "failed to list USB devices");

		for (i = 0; devs[i]; i++) {
			dev = devs[i];

			ret = usb_try_open(dev, qdl_usb, serial);
			if (ret == 1) {
				found = true;
				break;
			}
		}

		libusb_free_device_list(devs, 1);

		if (found)
			return 0;

		if (!wait_printed) {
			ux_info("Waiting for EDL device\n");
			wait_printed = true;
		}

		usleep(250000);
	}

	return -1;
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
	if ((len == actual) && !(actual % qdl_usb->in_maxpktsize)) {
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
