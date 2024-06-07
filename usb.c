#include <sys/ioctl.h>
#include <sys/types.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libusb.h>

#include "qdl.h"

/*
 * libusb commit f0cce43f882d ("core: Fix definition and use of enum
 * libusb_transfer_type") split transfer type and endpoint transfer types.
 * Provide an alias in order to make the code compile with the old (non-split)
 * definition.
 */
#ifndef LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK
#define LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK LIBUSB_TRANSFER_TYPE_BULK
#endif

static bool qdl_match_usb_serial(struct libusb_device_handle *handle, const char *serial,
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

static int qdl_try_open(libusb_device *dev, struct qdl_device *qdl, const char *serial)
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

	/* Consider only devices with vid 0x0506 and product id 0x9008 or 0x900e */
	if (desc.idVendor != 0x05c6 || (desc.idProduct != 0x9008 && desc.idProduct != 0x900e))
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

		if (!qdl_match_usb_serial(handle, serial, &desc)) {
			libusb_close(handle);
			continue;
		}

		ret = libusb_detach_kernel_driver(handle, ifc->bInterfaceNumber);
		if (ret < 0) {
			warnx("failed to detach USB interface %d", ret);
			libusb_close(handle);
			continue;
		}

		ret = libusb_claim_interface(handle, ifc->bInterfaceNumber);
		if (ret < 0) {
			warnx("failed to claim USB interface %d", ret);
			libusb_close(handle);
			continue;
		}

		qdl->usb_handle = handle;
		qdl->in_ep = in;
		qdl->out_ep = out;
		qdl->in_maxpktsize = in_size;
		qdl->out_maxpktsize = out_size;

		return 1;
	}

	return 0;
}

int qdl_open(struct qdl_device *qdl, const char *serial)
{
	struct libusb_device **devs;
	struct libusb_device *dev;
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

			ret = qdl_try_open(dev, qdl, serial);
			if (ret == 1) {
				found = true;
				break;
			}
		}

		libusb_free_device_list(devs, 1);

		if (found)
			return 0;

		if (!wait_printed) {
			fprintf(stderr, "Waiting for EDL device\n");
			wait_printed = true;
		}

		usleep(250000);
	}

	return -1;
}

int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout)
{
	int actual;
	int ret;

	ret = libusb_bulk_transfer(qdl->usb_handle, qdl->in_ep, buf, len, &actual, timeout);
	if (ret < 0)
		return -1;

	return actual;
}

int qdl_write(struct qdl_device *qdl, const void *buf, size_t len)
{
	unsigned char *data = (unsigned char*) buf;
	unsigned int count = 0;
	size_t len_orig = len;
	int actual;
	int xfer;
	int ret;

	while (len > 0) {
		xfer = (len > qdl->out_maxpktsize) ? qdl->out_maxpktsize : len;

		ret = libusb_bulk_transfer(qdl->usb_handle, qdl->out_ep, data,
					   xfer, &actual, 1000);
		if (ret < 0) {
			warnx("bulk write failed: %s", libusb_strerror(ret));
			return -1;
		}

		count += actual;
		len -= actual;
		data += actual;
	}

	if (len_orig % qdl->out_maxpktsize == 0) {
		ret = libusb_bulk_transfer(qdl->usb_handle, qdl->out_ep, NULL,
					   0, &actual, 1000);
		if (ret < 0)
			return -1;
	}

	return count;
}
