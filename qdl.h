/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __QDL_H__
#define __QDL_H__

#include <stdbool.h>

#include "patch.h"
#include "program.h"
#include "read.h"
#include <libxml/tree.h>
#include "vip.h"

#define container_of(ptr, typecast, member) ({                  \
	void *_ptr = (void *)(ptr);		                \
	((typeof(typecast) *)(_ptr - offsetof(typecast, member))); })

#define MIN(x, y) ({		\
	__typeof__(x) _x = (x);	\
	__typeof__(y) _y = (y);	\
	_x < _y ? _x : _y;	\
})

#define ROUND_UP(x, a) ({		\
	__typeof__(x) _x = (x);		\
	__typeof__(a) _a = (a);		\
	(_x + _a - 1) & ~(_a - 1);	\
})

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define MAPPING_SZ 64

enum QDL_DEVICE_TYPE {
	QDL_DEVICE_USB,
	QDL_DEVICE_SIM,
};

struct qdl_device {
	enum QDL_DEVICE_TYPE dev_type;
	int fd;
	size_t max_payload_size;
	size_t sector_size;

	int (*open)(struct qdl_device *qdl, const char *serial);
	int (*read)(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout);
	int (*write)(struct qdl_device *qdl, const void *buf, size_t nbytes);
	void (*close)(struct qdl_device *qdl);
	void (*set_out_chunk_size)(struct qdl_device *qdl, long size);
	void (*set_vip_transfer)(struct qdl_device *qdl, const char *signed_table,
				 const char *chained_table);

	char *mappings[MAPPING_SZ]; // array index is the id from the device

	struct vip_transfer_data vip_data;
};

struct libusb_device_handle;

struct qdl_device *qdl_init(enum QDL_DEVICE_TYPE type);
void qdl_deinit(struct qdl_device *qdl);
int qdl_open(struct qdl_device *qdl, const char *serial);
void qdl_close(struct qdl_device *qdl);
int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout);
int qdl_write(struct qdl_device *qdl, const void *buf, size_t len);
void qdl_set_out_chunk_size(struct qdl_device *qdl, long size);
int qdl_vip_transfer_enable(struct qdl_device *qdl, const char *vip_table_path);

struct qdl_device *usb_init(void);
struct qdl_device *sim_init(void);

int firehose_run(struct qdl_device *qdl, const char *incdir, const char *storage, bool allow_missing);
int sahara_run(struct qdl_device *qdl, char *img_arr[], bool single_image,
	       const char *ramdump_path, const char *ramdump_filter);
void print_hex_dump(const char *prefix, const void *buf, size_t len);
unsigned int attr_as_unsigned(xmlNode *node, const char *attr, int *errors);
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
