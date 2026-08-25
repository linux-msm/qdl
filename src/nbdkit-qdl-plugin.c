// SPDX-License-Identifier: BSD-3-Clause
/*
 * nbdkit plugin exposing a Qualcomm EDL device's storage as a block device.
 *
 * Based on the plugin from Bjorn Andersson's nbdkit branch, adapted to the
 * current qdl library interfaces.
 */
#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qdl.h"

/*
 * qdl uses ux_info(), ux_log() and ux_debug() helpers to write debug to stdout.
 * nbdkit redirects stdout to /dev/null before the first connection, so these
 * messages are normally discarded when qdl is used by the plugin.
 *
 * When the debug parameter is enabled, qdl_plugin_after_fork() redirects
 * stdout to stderr so qdl's debug output becomes visible alongside the nbdkit
 * log.
 */
bool qdl_debug;

/* UFS supports up to eight logical units (LUNs) per device. */
#define QDL_UFS_LUN_COUNT 8

static const char *config_programmer;
static enum qdl_storage_type config_storage = QDL_STORAGE_UFS;
static int config_lun;

/*
 * An EDL device is programmed once per session: uploading the firehose
 * programmer is a one-shot transition and a reset reboots the device out of
 * firehose. nbdkit, on the other hand, calls .open/.close several times per
 * client (NBD_OPT_INFO to query metadata, then NBD_OPT_GO for the transfer).
 *
 * So the device is set up once, on the first open, kept configured across
 * connections, and only torn down - with a reset - when the plugin unloads.
 * THREAD_MODEL_SERIALIZE_ALL_REQUESTS makes the shared state safe.
 */
static struct qdl_device *dev;
static size_t sector_size;
static size_t num_sectors;

static int qdl_plugin_config(const char *key, const char *value)
{
	int ret;

	if (!strcmp(key, "programmer")) {
		config_programmer = nbdkit_absolute_path(value);
		if (!config_programmer)
			return -1;
	} else if (!strcmp(key, "storage")) {
		config_storage = decode_storage_type(value);
		if (config_storage == QDL_STORAGE_UNKNOWN) {
			nbdkit_error("unknown storage type '%s'", value);
			return -1;
		}
	} else if (!strcmp(key, "lun")) {
		if (nbdkit_parse_int("lun", value, &config_lun) == -1)
			return -1;
		if (config_lun < 0 || config_lun >= QDL_UFS_LUN_COUNT) {
			nbdkit_error("lun must be between 0 and %d", QDL_UFS_LUN_COUNT - 1);
			return -1;
		}
	} else if (!strcmp(key, "debug")) {
		ret = nbdkit_parse_bool(value);
		if (ret == -1)
			return -1;
		qdl_debug = ret;
	} else {
		nbdkit_error("unknown parameter '%s'", key);
		return -1;
	}

	return 0;
}

static int qdl_plugin_config_complete(void)
{
	if (!config_programmer) {
		nbdkit_error("the 'programmer' parameter is required");
		return -1;
	}

	return 0;
}

/* Upload the programmer and configure firehose. Runs once, on the first open. */
static int qdl_device_setup(void)
{
	struct sahara_image images[MAPPING_SZ] = {};
	struct qdl_device *d;
	int ret;

	d = qdl_init(QDL_DEVICE_USB);
	if (!d) {
		nbdkit_error("failed to initialize USB backend");
		return -1;
	}

	if (qdl_open(d, NULL)) {
		nbdkit_error("failed to open EDL device");
		goto err_deinit;
	}

	if (load_sahara_image(NULL, config_programmer,
			      &images[SAHARA_ID_EHOSTDL_IMG]) < 0) {
		nbdkit_error("failed to load programmer '%s'", config_programmer);
		goto err_close;
	}

	ret = sahara_run(d, images, NULL, NULL);
	sahara_images_free(images, MAPPING_SZ);
	if (ret < 0) {
		nbdkit_error("failed to upload programmer");
		goto err_close;
	}

	if (firehose_open(d, config_storage) < 0) {
		nbdkit_error("failed to configure firehose programmer");
		goto err_close;
	}

	if (firehose_getsize(d, config_lun, &sector_size, &num_sectors) < 0) {
		nbdkit_error("failed to query size of LUN %d", config_lun);
		goto err_close;
	}

	dev = d;
	return 0;

err_close:
	qdl_close(d);
err_deinit:
	qdl_deinit(d);
	return -1;
}

/*
 * nbdkit redirects stdin and stdout to /dev/null after configuration, even in
 * foreground mode. This happens after .get_ready but before .after_fork,
 * making .after_fork the first callback where restoring stdout will persist.
 *
 * Redirect stdout to the same stderr stream used by nbdkit logging so qdl's
 * ux_*() output appears alongside nbdkit_debug() messages. This only makes
 * the output visible in foreground mode; when nbdkit daemonises, stderr is
 * also redirected to /dev/null.
 */
static int qdl_plugin_after_fork(void)
{
	if (!qdl_debug)
		return 0;

	if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
		nbdkit_error("failed to redirect stdout for debug output: %m");
		return -1;
	}

	nbdkit_debug("qdl debug output enabled; run nbdkit with -f to see it");

	return 0;
}

static void qdl_plugin_unload(void)
{
	if (!dev)
		return;

	firehose_reset(dev);
	qdl_close(dev);
	qdl_deinit(dev);
	dev = NULL;
}

static void *qdl_plugin_open(int readonly)
{
	(void)readonly;

	if (!dev && qdl_device_setup() < 0)
		return NULL;

	/* The device is shared; the handle is only used as a non-NULL token. */
	return dev;
}

static void qdl_plugin_close(void *handle)
{
	/* The device persists across connections; it is torn down in unload. */
	(void)handle;
}

static int64_t qdl_plugin_get_size(void *handle)
{
	(void)handle;

	return (int64_t)sector_size * num_sectors;
}

static int qdl_plugin_pread(void *handle, void *buf, uint32_t count,
			    uint64_t offset, uint32_t flags)
{
	(void)handle;
	(void)flags;

	if (offset % sector_size || count % sector_size) {
		nbdkit_error("unaligned read (sector size %zu)", sector_size);
		errno = EINVAL;
		return -1;
	}

	if (firehose_pread(dev, config_lun, offset / sector_size, buf,
			   sector_size, count / sector_size) < 0) {
		errno = EIO;
		return -1;
	}

	return 0;
}

static int qdl_plugin_pwrite(void *handle, const void *buf, uint32_t count,
			     uint64_t offset, uint32_t flags)
{
	(void)handle;
	(void)flags;

	if (offset % sector_size || count % sector_size) {
		nbdkit_error("unaligned write (sector size %zu)", sector_size);
		errno = EINVAL;
		return -1;
	}

	if (firehose_pwrite(dev, config_lun, offset / sector_size, buf,
			    sector_size, count / sector_size) < 0) {
		errno = EIO;
		return -1;
	}

	return 0;
}

static struct nbdkit_plugin plugin = {
	.name = "qdl",
	.description = "nbdkit Qualcomm Download plugin",
	.after_fork = qdl_plugin_after_fork,
	.unload = qdl_plugin_unload,
	.config = qdl_plugin_config,
	.config_complete = qdl_plugin_config_complete,
	.open = qdl_plugin_open,
	.close = qdl_plugin_close,
	.get_size = qdl_plugin_get_size,
	.pread = qdl_plugin_pread,
	.pwrite = qdl_plugin_pwrite,
};

NBDKIT_REGISTER_PLUGIN(plugin)
