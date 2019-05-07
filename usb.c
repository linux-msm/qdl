#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libudev.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "qdl.h"
#include "usb.h"

struct qdl_device {
	int fd;

	int in_ep;
	int out_ep;

	size_t in_maxpktsize;
	size_t out_maxpktsize;
};

static int parse_usb_desc(int fd, struct qdl_device *qdl, int *intf)
{
	const struct usb_interface_descriptor *ifc;
	const struct usb_endpoint_descriptor *ept;
	const struct usb_device_descriptor *dev;
	const struct usb_config_descriptor *cfg;
	const struct usb_descriptor_header *hdr;
	unsigned type;
	unsigned out;
	unsigned in;
	unsigned k;
	unsigned l;
	ssize_t n;
	size_t out_size;
	size_t in_size;
	void *ptr;
	void *end;
	char desc[1024];

	n = read(fd, desc, sizeof(desc));
	if (n < 0)
		return n;

	ptr = (void*)desc;
	end = ptr + n;

	dev = ptr;

	/* Consider only devices with vid 0x0506 and product id 0x9008 */
	if (dev->idVendor != 0x05c6 || dev->idProduct != 0x9008)
		return -EINVAL;

	ptr += dev->bLength;
	if (ptr >= end || dev->bDescriptorType != USB_DT_DEVICE)
		return -EINVAL;

	cfg = ptr;
	ptr += cfg->bLength;
	if (ptr >= end || cfg->bDescriptorType != USB_DT_CONFIG)
		return -EINVAL;

	for (k = 0; k < cfg->bNumInterfaces; k++) {
		if (ptr >= end)
			return -EINVAL;

		do {
			ifc = ptr;
			if (ifc->bLength < USB_DT_INTERFACE_SIZE)
				return -EINVAL;

			ptr += ifc->bLength;
		} while (ptr < end && ifc->bDescriptorType != USB_DT_INTERFACE);

		in = -1;
		out = -1;
		in_size = 0;
		out_size = 0;

		for (l = 0; l < ifc->bNumEndpoints; l++) {
			if (ptr >= end)
				return -EINVAL;

			do {
				ept = ptr;
				if (ept->bLength < USB_DT_ENDPOINT_SIZE)
					return -EINVAL;

				ptr += ept->bLength;
			} while (ptr < end && ept->bDescriptorType != USB_DT_ENDPOINT);

			type = ept->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
			if (type != USB_ENDPOINT_XFER_BULK)
				continue;

			if (ept->bEndpointAddress & USB_DIR_IN) {
				in = ept->bEndpointAddress;
				in_size = ept->wMaxPacketSize;
			} else {
				out = ept->bEndpointAddress;
				out_size = ept->wMaxPacketSize;
			}

			if (ptr >= end)
				break;

			hdr = ptr;
			if (hdr->bDescriptorType == USB_DT_SS_ENDPOINT_COMP)
				ptr += USB_DT_SS_EP_COMP_SIZE;
		}

		if (ifc->bInterfaceClass != 0xff)
			continue;

		if (ifc->bInterfaceSubClass != 0xff)
			continue;

		/* bInterfaceProtocol of 0xff and 0x10 has been seen */
		if (ifc->bInterfaceProtocol != 0xff &&
				ifc->bInterfaceProtocol != 16)
			continue;

		qdl->fd = fd;
		qdl->in_ep = in;
		qdl->out_ep = out;
		qdl->in_maxpktsize = in_size;
		qdl->out_maxpktsize = out_size;

		*intf = ifc->bInterfaceNumber;

		return 0;
	}

	return -ENOENT;
}

struct qdl_device *usb_open(void)
{
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices;
	struct udev_list_entry *dev_list_entry;
	struct udev_monitor *mon;
	struct udev_device *dev;
	struct qdl_device *qdl;
	const char *dev_node;
	struct udev *udev;
	const char *path;
	struct usbdevfs_ioctl cmd;
	int mon_fd;
	int intf = -1;
	int ret;
	int fd;

	qdl = calloc(1, sizeof(*qdl));

	udev = udev_new();
	if (!udev)
		err(1, "failed to initialize udev");

	mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(mon, "usb", NULL);
	udev_monitor_enable_receiving(mon);
	mon_fd = udev_monitor_get_fd(mon);

	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "usb");
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);

	udev_list_entry_foreach(dev_list_entry, devices) {
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);
		dev_node = udev_device_get_devnode(dev);

		if (!dev_node)
			continue;

		fd = open(dev_node, O_RDWR);
		if (fd < 0)
			continue;

		ret = parse_usb_desc(fd, qdl, &intf);
		if (!ret)
			goto found;

		close(fd);
	}

	fprintf(stderr, "Waiting for EDL device\n");

	for (;;) {
		fd_set rfds;

		FD_ZERO(&rfds);
		FD_SET(mon_fd, &rfds);

		ret = select(mon_fd + 1, &rfds, NULL, NULL, NULL);
		if (ret < 0)
			err(1, "failed to select");

		if (!FD_ISSET(mon_fd, &rfds))
			continue;

		dev = udev_monitor_receive_device(mon);
		dev_node = udev_device_get_devnode(dev);

		if (!dev_node)
			continue;

		printf("%s\n", dev_node);

		fd = open(dev_node, O_RDWR);
		if (fd < 0)
			continue;

		ret = parse_usb_desc(fd, qdl, &intf);
		if (!ret)
			goto found;

		close(fd);
	}

	udev_enumerate_unref(enumerate);
	udev_monitor_unref(mon);
	udev_unref(udev);

	return NULL;

found:
	udev_enumerate_unref(enumerate);
	udev_monitor_unref(mon);
	udev_unref(udev);

	cmd.ifno = intf;
	cmd.ioctl_code = USBDEVFS_DISCONNECT;
	cmd.data = NULL;

	ret = ioctl(qdl->fd, USBDEVFS_IOCTL, &cmd);
	if (ret && errno != ENODATA)
		err(1, "failed to disconnect kernel driver");

	ret = ioctl(qdl->fd, USBDEVFS_CLAIMINTERFACE, &intf);
	if (ret < 0)
		err(1, "failed to claim USB interface");

	return qdl;
}

int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout)
{
	struct usbdevfs_bulktransfer bulk = {};

	bulk.ep = qdl->in_ep;
	bulk.len = len;
	bulk.data = buf;
	bulk.timeout = timeout;

	return ioctl(qdl->fd, USBDEVFS_BULK, &bulk);
}

int qdl_write(struct qdl_device *qdl, const void *buf, size_t len, bool eot)
{

	unsigned char *data = (unsigned char*) buf;
	struct usbdevfs_bulktransfer bulk = {};
	unsigned count = 0;
	size_t len_orig = len;
	int n;

	if(len == 0) {
		bulk.ep = qdl->out_ep;
		bulk.len = 0;
		bulk.data = data;
		bulk.timeout = 1000;

		n = ioctl(qdl->fd, USBDEVFS_BULK, &bulk);
		if(n != 0) {
			fprintf(stderr,"ERROR: n = %d, errno = %d (%s)\n",
				n, errno, strerror(errno));
			return -1;
		}
		return 0;
	}

	while(len > 0) {
		int xfer;
		xfer = (len > qdl->out_maxpktsize) ? qdl->out_maxpktsize : len;

		bulk.ep = qdl->out_ep;
		bulk.len = xfer;
		bulk.data = data;
		bulk.timeout = 1000;

		n = ioctl(qdl->fd, USBDEVFS_BULK, &bulk);
		if(n != xfer) {
			fprintf(stderr, "ERROR: n = %d, errno = %d (%s)\n",
				n, errno, strerror(errno));
			return -1;
		}
		count += xfer;
		len -= xfer;
		data += xfer;
	}

	if (eot && (len_orig % qdl->out_maxpktsize) == 0) {
		bulk.ep = qdl->out_ep;
		bulk.len = 0;
		bulk.data = NULL;
		bulk.timeout = 1000;

		n = ioctl(qdl->fd, USBDEVFS_BULK, &bulk);
		if (n < 0)
			return n;
	}

	return count;
}
