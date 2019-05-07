#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

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
#include "patch.h"
#include "ufs.h"
#include "usb.h"

static const char *config_programmer;
static bool config_ufs = true;
static int config_lun = 1;

static size_t sector_size;

static int qdl_config(const char *key, const char *value)
{
        if (!strcmp(key, "programmer")) {
                config_programmer = nbdkit_absolute_path(value);
        } else if (!strcmp(key, "storage")) {
                if (!strcmp(value, "ufs")) {
                        config_ufs = true;
                } else if (!strcmp(value, "emmc")) {
                        config_ufs = false;
                } else {
                        nbdkit_error("unknown storage type '%s'", value);
                        return -1;
                }
        } else if (!strcmp(key, "lun")) {
                config_lun = atoi(value);
        } else {
                return -1;
        }

        return 0;
}

static int qdl_config_complete(void)
{
        if (!config_programmer)
                return -1;

        return 0;
}

static void *qdl_open(int readonly)
{
        struct qdl_device *qdl;
        int ret;

        qdl = usb_open();
        if (!qdl)
                return NULL;

        ret = sahara_run(qdl, config_programmer);
        if (ret < 0)
                return NULL;

        ret = firehose_open(qdl, config_ufs);
        if (ret < 0)
                return NULL;

        return qdl;
}

static void qdl_close(void *handle)
{
        struct qdl_device *qdl = handle;

        firehose_reset(qdl);

        free(qdl);
}

static int64_t qdl_get_size(void *handle)
{
        struct qdl_device *qdl = handle;
        size_t num_sectors;
        int ret;

        ret = firehose_getsize(qdl, config_lun, &sector_size, &num_sectors);
        if (ret < 0)
                return -1;

        return sector_size * num_sectors;
}

static int qdl_pread(void *handle, void *buf, uint32_t count, uint64_t offset,
                     uint32_t flags)
{
        struct qdl_device *qdl = handle;

        return firehose_pread(qdl, config_lun, offset / sector_size, buf,
                              sector_size, count / sector_size);
}

static int qdl_pwrite(void *handle, const void *buf, uint32_t count,
                      uint64_t offset, uint32_t flags)
{
        struct qdl_device *qdl = handle;

        return firehose_pwrite(qdl, config_lun, offset / sector_size, buf,
                               sector_size, count / sector_size);
}

static struct nbdkit_plugin qdl_plugin = {
        .name = "qdl",
        .description = "nbdkit Qualcomm Download plugin",
        .open = qdl_open,
        .close = qdl_close,
        .config = qdl_config,
        .config_complete = qdl_config_complete,
        .get_size = qdl_get_size,
        .pread = qdl_pread,
        .pwrite = qdl_pwrite,
};

NBDKIT_REGISTER_PLUGIN(qdl_plugin)
