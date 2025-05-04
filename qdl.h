#ifndef __QDL_H__
#define __QDL_H__

#include <stdbool.h>

#include "patch.h"
#include "program.h"
#include "read.h"
#include <libxml/tree.h>

#define MAPPING_SZ 64

struct libusb_device_handle;

struct qdl_device {
        struct libusb_device_handle *usb_handle;
        int fd;

        int in_ep;
        int out_ep;

        size_t in_maxpktsize;
        size_t out_maxpktsize;
        size_t out_chunk_size;

        char *mappings[MAPPING_SZ]; // array index is the id from the device
};

int qdl_open(struct qdl_device *qdl, const char *serial);
void qdl_close(struct qdl_device *qdl);
int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout);
int qdl_write(struct qdl_device *qdl, const void *buf, size_t len);
void qdl_set_out_chunk_size(struct qdl_device *qdl, long size);

int firehose_run(struct qdl_device *qdl, const char *incdir, const char *storage, bool allow_missing);
int sahara_run(struct qdl_device *qdl, char *img_arr[], bool single_image,
	       const char *ramdump_path, const char *ramdump_filter);
void print_hex_dump(const char *prefix, const void *buf, size_t len);
unsigned attr_as_unsigned(xmlNode *node, const char *attr, int *errors);
const char *attr_as_string(xmlNode *node, const char *attr, int *errors);
bool attr_as_bool(xmlNode *node, const char *attr, int *errors);

void ux_init(void);
void ux_err(const char *fmt, ...);
void ux_info(const char *fmt, ...);
void ux_log(const char *fmt, ...);
void ux_debug(const char *fmt, ...);
void ux_progress(const char *fmt, unsigned int value, unsigned int size, ...);

void print_version(void);

extern bool qdl_debug;

#endif
