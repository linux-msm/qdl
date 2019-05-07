#ifndef __QDL_USB_H__
#define __QDL_USB_H__

struct qdl_device;

struct qdl_device *usb_open(void);
int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout);
int qdl_write(struct qdl_device *qdl, const void *buf, size_t len, bool eot);

#endif
