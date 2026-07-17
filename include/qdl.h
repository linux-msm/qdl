/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __QDL_H__
#define __QDL_H__

#ifdef _WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif

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

#define __unused __attribute__((__unused__))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ALIGN_UP(p, size) ({						\
		__typeof__(size) _mask = (size) - 1;			\
		(__typeof__(p))(((uintptr_t)(p) + _mask) & ~_mask);	\
})

#define MAPPING_SZ 128

#define SAHARA_ID_EHOSTDL_IMG	13

enum QDL_DEVICE_TYPE {
	QDL_DEVICE_USB,
	QDL_DEVICE_SIM,
	QDL_DEVICE_QUD,
	/*
	 * Meta-backend: defers transport selection to the wait loop inside
	 * auto_open(), which polls libusb and (on Windows) the QUD SetupAPI
	 * enumeration each tick and binds whichever first reaches an EDL
	 * device. Resolves the UX hazard of an upfront probe timeout where
	 * the user plugs in the cable just after the grace window expires.
	 */
	QDL_DEVICE_AUTO,
};

enum qdl_storage_type {
	QDL_STORAGE_UNKNOWN,
	QDL_STORAGE_EMMC,
	QDL_STORAGE_NAND,
	QDL_STORAGE_UFS,
	QDL_STORAGE_NVME,
	QDL_STORAGE_SPINOR,
};

enum qdl_skipblock_mode {
	QDL_SKIPBLOCK_NONE,
	QDL_SKIPBLOCK_SHA256,
};

struct qdl_device {
	enum QDL_DEVICE_TYPE dev_type;
	int fd;
	size_t max_payload_size;
	size_t sector_size;
	enum qdl_storage_type current_storage_type;
	enum qdl_skipblock_mode skipblock_mode;
	unsigned int slot;

	int (*open)(struct qdl_device *qdl, const char *serial);
	int (*read)(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout);
	int (*write)(struct qdl_device *qdl, const void *buf, size_t nbytes, unsigned int timeout);
	void (*close)(struct qdl_device *qdl);
	void (*set_out_chunk_size)(struct qdl_device *qdl, long size);
	void (*set_vip_transfer)(struct qdl_device *qdl, const char *signed_table,
				 const char *chained_table);

	struct vip_transfer_data vip_data;

	/*
	 * Pushback buffer for stream-oriented transports (Windows COM via the
	 * QDLoader driver, virtio-console, ...). When a single read crosses a
	 * Firehose message boundary - typically because the binary payload of
	 * a rawmode response trails the XML envelope in the same read - the
	 * leftover bytes are stashed here and qdl_read() returns them before
	 * pulling more data from the transport.
	 */
	char *pending_buf;
	size_t pending_len;
	size_t pending_off;
};

struct sahara_image {
	char *name;
	void *ptr;
	size_t len;
};

struct qdl_zip;

struct libusb_device_handle;

struct qdl_device *qdl_init(enum QDL_DEVICE_TYPE type);
void qdl_deinit(struct qdl_device *qdl);
int qdl_open(struct qdl_device *qdl, const char *serial);
void qdl_close(struct qdl_device *qdl);
int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout);
int qdl_push_back(struct qdl_device *qdl, const void *buf, size_t len);
int qdl_write(struct qdl_device *qdl, const void *buf, size_t len, unsigned int timeout);
void qdl_set_out_chunk_size(struct qdl_device *qdl, long size);
int qdl_vip_transfer_enable(struct qdl_device *qdl, const char *vip_table_path);

struct qdl_device *usb_init(void);
struct qdl_device *sim_init(void);
struct qdl_device *qud_init(void);
struct qdl_device *auto_init(void);

/*
 * usb_open_once() - single libusb scan-and-open pass; shared between
 * the --backend usb wait loop in usb.c and the unified auto_open() loop.
 * Returns 0 on success (and emits the "Flashing/Collecting device" UX
 * line), -ENODEV when no EDL device is visible, -EBUSY when one is
 * visible but cannot be opened (typically: the Qualcomm QDLoader 9008
 * driver has claimed it), -EIO on a libusb failure. @visible_out, if
 * non-NULL, receives the count of EDL devices seen on this pass.
 *
 * qud_probe_present() returns the number of Qualcomm COM ports the QUD
 * backend enumerated via SetupAPI; 0 on non-Windows hosts.
 */
int usb_open_once(struct qdl_device *qdl, const char *serial, int *visible_out);
int qud_probe_present(void);

/*
 * EDL device identity, shared by all transport backends: Qualcomm's
 * vendor id plus the known EDL/ramdump product ids. The libusb backend
 * additionally requires a matching vendor-specific interface (see
 * usb_match_edl_interface() in usb.c); on Windows the QDLoader driver
 * performs the equivalent match before the QUD backend sees the device.
 */
#define QUALCOMM_VID 0x05c6

static inline bool qdl_is_edl_pid(unsigned int pid)
{
	return pid == 0x9008 || pid == 0x900e || pid == 0x901d || pid == 0x90db;
}

static inline bool qdl_is_edl_device(unsigned int vid, unsigned int pid)
{
	return vid == QUALCOMM_VID && qdl_is_edl_pid(pid);
}

struct qdl_device_desc {
	int vid;
	int pid;
	char serial[16];
};

struct qdl_device_desc *usb_list(unsigned int *devices_found);

/*
 * QUD-side counterpart to qdl_device_desc. Serial here is the iSerial as
 * Windows stored it in the device-instance ID (no fixed length guarantee
 * across OEMs), and path is the kernel-driver-exposed handle the QUD
 * backend will open (e.g. "\\\\.\\COM5").
 */
struct qud_device_desc {
	unsigned int pid;
	char serial[64];
	char path[64];
};

struct qud_device_desc *qud_list(unsigned int *devices_found);

int firehose_run(struct qdl_device *qdl, struct list_head *ops);
int firehose_provision(struct qdl_device *qdl, bool skip_reset);
int firehose_read_buf(struct qdl_device *qdl, struct firehose_op *read_op, void *out_buf, size_t out_size);

/* Block-level entry points used by the nbdkit plugin */
int firehose_open(struct qdl_device *qdl, enum qdl_storage_type storage);
int firehose_reset(struct qdl_device *qdl);
int firehose_getsize(struct qdl_device *qdl, int lun, size_t *sector_size,
		     size_t *num_sectors);
ssize_t firehose_pread(struct qdl_device *qdl, int lun, size_t sector_offset,
		       void *buf, size_t sector_size, size_t num_sectors);
ssize_t firehose_pwrite(struct qdl_device *qdl, int lun, size_t sector_offset,
			const void *buf, size_t sector_size, size_t num_sectors);
int sahara_run(struct qdl_device *qdl, const struct sahara_image *images,
	       const char *ramdump_path,
	       const char *ramdump_filter);
int sahara_chipinfo(struct qdl_device *qdl);
int load_sahara_image(struct qdl_zip *zip, const char *filename, struct sahara_image *image);
void sahara_images_free(struct sahara_image *images, size_t count);
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

int parse_storage_address(const char *address, int *physical_partition,
			  unsigned int *start_sector, unsigned int *num_sectors,
			  char **gpt_partition);

enum qdl_storage_type decode_storage_type(const char *storage);
const char *encode_storage_type(enum qdl_storage_type storage);
int decode_sahara_config(struct sahara_image *blob, struct sahara_image *images,
			 struct contents_filter *contents_filter);

extern bool qdl_debug;

#endif
