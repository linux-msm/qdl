// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * All rights reserved.
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <unistd.h>

#include "qdl.h"
#include "contents.h"
#include "programmer.h"
#include "file.h"
#include "firehose.h"
#include "flashmap.h"
#include "patch.h"
#include "pathbuf.h"
#include "program.h"
#include "ufs.h"
#include "oscompat.h"
#include "vip.h"

#ifdef _WIN32
const char *__progname = "qdl";
#endif

#define MAX_USBFS_BULK_SIZE	(16 * 1024)

enum {
	QDL_FILE_UNKNOWN,
	QDL_FILE_PATCH,
	QDL_FILE_PROGRAM,
	QDL_FILE_READ,
	QDL_FILE_UFS,
	QDL_FILE_CONTENTS,
	QDL_CMD_READ,
	QDL_CMD_WRITE,
	QDL_CMD_ERASE,
	QDL_CMD_FLASH,
	QDL_CMD_SHA256,
	QDL_CMD_RESET,
};

bool qdl_debug;

static int detect_type(const char *verb)
{
	xmlNode *root;
	xmlDoc *doc;
	xmlNode *node;
	int type = QDL_FILE_UNKNOWN;

	if (!strcmp(verb, "read"))
		return QDL_CMD_READ;
	if (!strcmp(verb, "write"))
		return QDL_CMD_WRITE;
	if (!strcmp(verb, "erase"))
		return QDL_CMD_ERASE;
	if (!strcmp(verb, "flash"))
		return QDL_CMD_FLASH;
	if (!strcmp(verb, "sha256"))
		return QDL_CMD_SHA256;
	if (!strcmp(verb, "reset"))
		return QDL_CMD_RESET;

	if (access(verb, F_OK)) {
		ux_err("%s is not a verb and not a XML file\n", verb);
		return -EINVAL;
	}

	doc = xmlReadFile(verb, NULL, 0);
	if (!doc) {
		ux_err("failed to parse XML file \"%s\"\n", verb);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	if (!xmlStrcmp(root->name, (xmlChar *)"patches")) {
		type = QDL_FILE_PATCH;
	} else if (!xmlStrcmp(root->name, (xmlChar *)"data")) {
		for (node = root->children; node ; node = node->next) {
			if (node->type != XML_ELEMENT_NODE)
				continue;
			if (!xmlStrcmp(node->name, (xmlChar *)"program")) {
				type = QDL_FILE_PROGRAM;
				break;
			}
			if (!xmlStrcmp(node->name, (xmlChar *)"read")) {
				type = QDL_FILE_READ;
				break;
			}
			if (!xmlStrcmp(node->name, (xmlChar *)"ufs")) {
				type = QDL_FILE_UFS;
				break;
			}
		}
	} else if (!xmlStrcmp(root->name, (xmlChar *)"contents")) {
		type = QDL_FILE_CONTENTS;
	}

	xmlFreeDoc(doc);

	return type;
}

/*
 * Parse a --backend= value into an enum. "auto" maps to the meta-backend
 * QDL_DEVICE_AUTO, which inside its open path runs a unified wait loop
 * over libusb and (on Windows) the QUD SetupAPI enumeration, binding
 * whichever first reaches an EDL device. Explicit "usb"/"qud" pin to a
 * single concrete transport and skip the meta layer entirely.
 *
 * QDL_DEVICE_SIM is intentionally not selectable via --backend; --dry-run /
 * --create-digests pick it implicitly.
 */
static int decode_backend(const char *name, enum QDL_DEVICE_TYPE *out)
{
	if (!name || !strcmp(name, "auto")) {
		*out = QDL_DEVICE_AUTO;
		return 0;
	}

	if (!strcmp(name, "usb")) {
		*out = QDL_DEVICE_USB;
		return 0;
	}

	if (!strcmp(name, "qud")) {
		*out = QDL_DEVICE_QUD;
		return 0;
	}

	return -1;
}


static void print_usage(FILE *out)
{
	extern const char *__progname;

	fprintf(out, "Usage: %s [options] <prog.mbn> (<program-xml> | <patch-xml> | <read-xml>)...\n", __progname);
	fprintf(out, "       %s [options] <prog.mbn> ((read | write) <address> <binary>)...\n", __progname);
	fprintf(out, "       %s [options] <prog.mbn> (erase <address>)...\n", __progname);
	fprintf(out, "       %s [options] <prog.mbn> (sha256 <address>)...\n", __progname);
	fprintf(out, "       %s [options] <prog.mbn> (reset)\n", __progname);
	fprintf(out, "       %s list\n", __progname);
	fprintf(out, "       %s chipinfo\n", __progname);
	fprintf(out, "       %s reset\n", __progname);
	fprintf(out, "       %s ramdump [--debug] [-o <ramdump-path>] [<segment-filter>,...]\n", __progname);
	fprintf(out, "       %s ks [-p <sahara-dev-node> | --serial=T] -s <id:file-path>...\n", __progname);
	fprintf(out, "       %s flash (<flashmap>[::specifier] | <contents>[::<specifier>])\n", __progname);
	fprintf(out, "       %s create-zip <zipfile> <contents>[::<specifier>]\n", __progname);
	fprintf(out, "       %s create-sahara-archive <archive.bin> "
		"(<id:file>[,<id:file>...] | <sahara.xml> | <contents.xml>[::<specifier>])\n",
		__progname);
	fprintf(out, " -d, --debug\t\t\tPrint detailed debug info\n");
	fprintf(out, " -v, --version\t\t\tPrint the current version and exit\n");
	fprintf(out, " -n, --dry-run\t\t\tDry run execution, no device reading or flashing\n");
	fprintf(out, " -f, --allow-missing\t\tAllow skipping of missing files during flashing\n");
	fprintf(out, " -s, --storage=T\t\tSet target storage type T: <emmc|nand|nvme|spinor|ufs>\n");
	fprintf(out, " -l, --finalize-provisioning\tProvision the target storage\n");
	fprintf(out, " -i, --include=T\t\tSet an optional folder T to search for files\n");
	fprintf(out, " -S, --serial=T\t\t\tSelect target by serial number T (e.g. <0AA94EFD>)\n");
	fprintf(out, " -u, --out-chunk-size=T\t\tOverride chunk size for transaction with T\n");
	fprintf(out, " -t, --create-digests=T\t\tGenerate table of digests in the T folder\n");
	fprintf(out, " -T, --slot=T\t\t\tSet slot number T for multiple storage devices\n");
	fprintf(out, " -D, --vip-table-path=T\t\tUse digest tables in the T folder for VIP\n");
	fprintf(out, " -R, --skip-reset\t\tDo not send the reset command after flashing completes\n");
	fprintf(out, "     --backend=B\t\tSelect device backend B: <auto|usb|qud> (default: auto)\n");
	fprintf(out, "     --skipblock=M\t\tUse readback mechanism M to skip <program> entries already on flash;\n");
	fprintf(out, "                 \t\tM: <none|sha256> (default: none)\n");
	fprintf(out, " -h, --help\t\t\tPrint this usage info\n");
	fprintf(out, " <program-xml>\t\txml file containing <program> or <erase> directives\n");
	fprintf(out, " <patch-xml>\t\txml file containing <patch> directives\n");
	fprintf(out, " <read-xml>\t\txml file containing <read> directives\n");
	fprintf(out, " <address>\t\tdisk address specifier, can be one of <P>, <P/S>, <P/S+L>, <name>, or\n");
	fprintf(out, "          \t\t<P/name>, to specify a physical partition number P, a starting sector\n");
	fprintf(out, "          \t\tnumber S, the number of sectors to follow L, or partition by \"name\"\n");
	fprintf(out, " <ramdump-path>\t\tpath where ramdump should stored\n");
	fprintf(out, " <segment-filter>\toptional glob-pattern to select which segments to ramdump\n");
	fprintf(out, " <sahara-dev-node>\tSahara device node, e.g. /dev/mhi0_QAIC_SAHARA (ks);\n");
	fprintf(out, "                 \tomit to use the selected device backend (ks)\n");
	fprintf(out, " <id:file-path>\t\tmap a Sahara image id to a host file, repeatable (ks)\n");
	fprintf(out, " <flashmap>\tflashmap JSON file, or ZIP archive with flashmap.json\n");
	fprintf(out, " <contents>\tcontents XML file\n");
	fprintf(out, " <archive.bin>\tSahara programmer archive to create\n");
	fprintf(out, " <specifier>\tcomma-separated list of specifiers, such as storage type, layout, and flavors\n");
	fprintf(out, "\n");
	fprintf(out, "Example: %s prog_firehose_ddr.elf rawprogram*.xml patch*.xml\n", __progname);
	fprintf(out, "         %s flash contents.xml::ufs,spinor/safe_rtos\n", __progname);
	fprintf(out, "         %s flash installer.zip::layout1/ufs\n", __progname);
}

static int qdl_list(FILE *out)
{
	struct qdl_device_desc *usb_devices;
	struct qud_device_desc *qud_devices;
	unsigned int usb_count = 0;
	unsigned int qud_count = 0;
	unsigned int i;

	usb_devices = usb_list(&usb_count);
	qud_devices = qud_list(&qud_count);

	if (usb_count == 0 && qud_count == 0) {
		fprintf(out, "No devices found\n");
	} else {
		for (i = 0; i < usb_count; i++)
			fprintf(out, "%04x:%04x\t%s\n",
				usb_devices[i].vid, usb_devices[i].pid,
				usb_devices[i].serial);
		for (i = 0; i < qud_count; i++)
			fprintf(out, "05c6:%04x\t%s\t%s\n",
				qud_devices[i].pid,
				qud_devices[i].serial,
				qud_devices[i].path);
	}

	free(usb_devices);
	free(qud_devices);

	return 0;
}

/* Long-only option ids, distinct from any short option character. */
enum {
	OPT_BACKEND = 0x100,
	OPT_SKIPBLOCK,
	OPT_SERIAL,
};

/* Results of qdl_common_opt() for options every subcommand shares. */
enum {
	QDL_OPT_HANDLED,
	QDL_OPT_EXIT_OK,
	QDL_OPT_EXIT_FAIL,
};

/* Long options for subcommands that take no extra options. */
static const struct option qdl_common_options[] = {
	{"debug", no_argument, 0, 'd'},
	{"version", no_argument, 0, 'v'},
	{"serial", required_argument, 0, 'S'},
	{"backend", required_argument, 0, OPT_BACKEND},
	{"help", no_argument, 0, 'h'},
	{0, 0, 0, 0}
};

/*
 * Handle the option cases shared by every subcommand: --debug,
 * --version, --serial, --backend and --help, plus the unknown-option
 * fallback. Returns QDL_OPT_HANDLED when parsing should continue, or
 * QDL_OPT_EXIT_OK / QDL_OPT_EXIT_FAIL when the caller should return
 * with success / failure.
 */
static int qdl_common_opt(int opt, char **serial, enum QDL_DEVICE_TYPE *dev_type)
{
	switch (opt) {
	case 'd':
		qdl_debug = true;
		return QDL_OPT_HANDLED;
	case 'v':
		print_version();
		return QDL_OPT_EXIT_OK;
	case 'S':
	case OPT_SERIAL:
		*serial = optarg;
		return QDL_OPT_HANDLED;
	case OPT_BACKEND:
		if (decode_backend(optarg, dev_type) < 0)
			errx(1, "unknown backend \"%s\" (expected auto|usb|qud)", optarg);
		return QDL_OPT_HANDLED;
	case 'h':
		print_usage(stdout);
		return QDL_OPT_EXIT_OK;
	default:
		print_usage(stderr);
		return QDL_OPT_EXIT_FAIL;
	}
}

/*
 * Common device session setup for the subcommands: initialize the
 * requested backend and open the (optionally serial-selected) device.
 * Returns NULL with the error already reported on failure.
 */
static struct qdl_device *qdl_session_open(enum QDL_DEVICE_TYPE dev_type, const char *serial)
{
	struct qdl_device *qdl;

	qdl = qdl_init(dev_type);
	if (!qdl) {
		ux_err("backend not available\n");
		return NULL;
	}

	if (qdl_debug)
		print_version();

	if (qdl_open(qdl, serial)) {
		qdl_deinit(qdl);
		return NULL;
	}

	return qdl;
}

static void qdl_session_close(struct qdl_device *qdl)
{
	qdl_close(qdl);
	qdl_deinit(qdl);
}

static int qdl_ramdump(int argc, char **argv)
{
	struct qdl_device *qdl;
	char *ramdump_path = ".";
	char *filter = NULL;
	char *serial = NULL;
	enum QDL_DEVICE_TYPE qdl_dev_type = QDL_DEVICE_AUTO;
	int ret = 0;
	int opt;

	static struct option options[] = {
		{"debug", no_argument, 0, 'd'},
		{"version", no_argument, 0, 'v'},
		{"output", required_argument, 0, 'o'},
		{"serial", required_argument, 0, 'S'},
		{"backend", required_argument, 0, OPT_BACKEND},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "dvo:S:h", options, NULL)) != -1) {
		if (opt == 'o') {
			ramdump_path = optarg;
			continue;
		}

		ret = qdl_common_opt(opt, &serial, &qdl_dev_type);
		if (ret == QDL_OPT_EXIT_OK)
			return 0;
		if (ret == QDL_OPT_EXIT_FAIL)
			return 1;
	}

	if (optind < argc)
		filter = argv[optind++];

	if (optind != argc) {
		print_usage(stderr);
		return 1;
	}

	ux_init();

	if (qdl_mkdir_p(ramdump_path) < 0) {
		ux_err("failed to create ramdump directory \"%s\": %s\n",
		       ramdump_path, strerror(errno));
		return 1;
	}

	qdl = qdl_session_open(qdl_dev_type, serial);
	if (!qdl)
		return 1;

	ret = sahara_run(qdl, NULL, ramdump_path, filter) < 0 ? 1 : 0;

	qdl_session_close(qdl);

	return ret;
}

/*
 * Shared entry point for Sahara-only subcommands that take no options
 * beyond the common set and no positional arguments: parse the options,
 * open the device session and hand it to @run. Returns the subcommand
 * exit code; @run reports failure with a negative return value.
 */
static int qdl_sahara_cmd(int argc, char **argv, int (*run)(struct qdl_device *qdl))
{
	struct qdl_device *qdl;
	char *serial = NULL;
	enum QDL_DEVICE_TYPE qdl_dev_type = QDL_DEVICE_AUTO;
	int ret;
	int opt;

	while ((opt = getopt_long(argc, argv, "dvS:h", qdl_common_options, NULL)) != -1) {
		ret = qdl_common_opt(opt, &serial, &qdl_dev_type);
		if (ret == QDL_OPT_EXIT_OK)
			return 0;
		if (ret == QDL_OPT_EXIT_FAIL)
			return 1;
	}

	if (optind != argc) {
		print_usage(stderr);
		return 1;
	}

	ux_init();

	qdl = qdl_session_open(qdl_dev_type, serial);
	if (!qdl)
		return 1;

	ret = run(qdl) < 0 ? 1 : 0;

	qdl_session_close(qdl);

	return ret;
}

/*
 * Device reset ("reset") subcommand body.
 *
 * Resets the device from whichever state it is in: over the Sahara
 * protocol when the device sits in EDL or crash mode, falling back to a
 * Firehose power reset when a programmer is already running. Unlike the
 * "reset" flashing verb, no programmer needs to be uploaded.
 */
static int qdl_reset_run(struct qdl_device *qdl)
{
	int ret;

	ret = sahara_device_reset(qdl);
	if (ret == 1) {
		ux_info("falling back to Firehose reset\n");
		ret = firehose_reset(qdl);
	}

	return ret;
}

/*
 * Sahara kickstart ("ks") subcommand.
 *
 * Kickstart serves Sahara images to devices that fetch their firmware from the
 * host rather than from local storage, and stops once Sahara is done: no
 * Firehose programmer is involved and nothing is written to storage.
 *
 * Two transports are available. A kernel-provided Sahara device node (such as
 * /dev/mhi0_QAIC_SAHARA) is selected with -p and driven with plain
 * read()/write() through the raw-fd hooks below. When -p is omitted the same
 * backends the other subcommands use are allocated instead, so an EDL device
 * on the USB bus can be kickstarted directly.
 */
static int ks_read(struct qdl_device *qdl, void *buf, size_t len,
		   unsigned int timeout __unused)
{
	return read(qdl->fd, buf, len);
}

static int ks_write(struct qdl_device *qdl, const void *buf, size_t len,
		    unsigned int timeout __unused)
{
	return write(qdl->fd, buf, len);
}

/**
 * ks_add_mapping() - record a "<id>:<path>" image mapping
 * @arg: option argument to parse
 * @mappings: image array to populate, indexed by Sahara image id
 *
 * Returns: 0 on success, -1 on failure.
 */
static int ks_add_mapping(const char *arg, struct sahara_image *mappings)
{
	const char *filename;
	const char *colon;
	long file_id;

	file_id = strtol(arg, NULL, 10);
	colon = strchr(arg, ':');
	if (file_id < 0 || file_id >= MAPPING_SZ || !colon) {
		ux_err("invalid sahara mapping \"%s\", expected <id:path> with id in 0..%d\n",
		       arg, MAPPING_SZ - 1);
		return -1;
	}

	filename = colon + 1;
	if (load_sahara_image(NULL, filename, &mappings[file_id]) < 0)
		return -1;

	ux_debug("mapped sahara image id %ld to %s\n", file_id, filename);

	return 0;
}

static int qdl_ks(int argc, char **argv)
{
	struct sahara_image mappings[MAPPING_SZ] = {};
	struct qdl_device node_dev = {
		.fd = -1,
		.read = ks_read,
		.write = ks_write,
	};
	enum QDL_DEVICE_TYPE qdl_dev_type = QDL_DEVICE_AUTO;
	struct qdl_device *qdl = NULL;
	bool found_mapping = false;
	const char *dev_node = NULL;
	char *serial = NULL;
	int ret = 0;
	int opt;

	static struct option options[] = {
		{"debug", no_argument, 0, 'd'},
		{"version", no_argument, 0, 'v'},
		{"port", required_argument, 0, 'p'},
		{"sahara", required_argument, 0, 's'},
		{"serial", required_argument, 0, OPT_SERIAL},
		{"backend", required_argument, 0, OPT_BACKEND},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "dvp:s:h", options, NULL)) != -1) {
		switch (opt) {
		case 'p':
			dev_node = optarg;
			break;
		case 's':
			if (ks_add_mapping(optarg, mappings) < 0) {
				ret = 1;
				goto out;
			}
			found_mapping = true;
			break;
		default:
			ret = qdl_common_opt(opt, &serial, &qdl_dev_type);
			if (ret == QDL_OPT_EXIT_OK) {
				ret = 0;
				goto out;
			}
			if (ret == QDL_OPT_EXIT_FAIL) {
				ret = 1;
				goto out;
			}
			ret = 0;
		}
	}

	/* At least one image mapping (-s) is required */
	if (!found_mapping) {
		print_usage(stderr);
		ret = 1;
		goto out;
	}

	/*
	 * -p names the transport explicitly, so device selection options that
	 * only apply to the enumerated backends would be silently ignored.
	 */
	if (dev_node && (serial || qdl_dev_type != QDL_DEVICE_AUTO)) {
		ux_err("--port cannot be combined with --serial or --backend\n");
		ret = 1;
		goto out;
	}

	ux_init();

	if (dev_node) {
		if (qdl_debug)
			print_version();

		node_dev.fd = qdl_open_device_node(dev_node);
		if (node_dev.fd < 0) {
			ux_err("unable to open %s: %s\n", dev_node, strerror(errno));
			ret = 1;
			goto out;
		}
		qdl = &node_dev;
	} else {
		qdl = qdl_session_open(qdl_dev_type, serial);
		if (!qdl) {
			ret = 1;
			goto out;
		}
	}

	if (sahara_run(qdl, mappings, NULL, NULL) < 0)
		ret = 1;

	if (dev_node)
		close(node_dev.fd);
	else
		qdl_session_close(qdl);
out:
	sahara_images_free(mappings, MAPPING_SZ);
	return ret;
}

static int qdl_ensure_configured(struct list_head *ops, enum qdl_storage_type storage_type)
{
	struct firehose_op *op;

	if (list_empty(ops))
		return 0;

	op = list_entry_first(ops, struct firehose_op, node);
	if (op->type == FIREHOSE_OP_CONFIGURE)
		return 0;

	op = firehose_alloc_op(FIREHOSE_OP_CONFIGURE);
	if (!op)
		return -1;

	op->storage_type = storage_type;

	list_prepend(ops, &op->node);

	return 0;
}

static char *qdl_split_specifier(const char *param, char **specifier)
{
	char *filename;
	char *tmp;

	if (!param || !param[0])
		return NULL;

	filename = strdup(param);
	if (!filename) {
		ux_err("internal error: unable to allocate memory for argument\n");
		return NULL;
	}

	*specifier = NULL;

	tmp = strstr(filename, "::");
	if (tmp) {
		if (strstr(tmp + 2, "::")) {
			free(filename);
			return NULL;
		}

		*tmp = '\0';
		if (!filename[0] || !tmp[2]) {
			free(filename);
			return NULL;
		}

		*specifier = tmp + 2;
	}

	return filename;
}

static int qdl_cmd_flash(struct list_head *firehose_ops, const char *arg,
			 const char *incdir, struct sahara_image *images)
{
	struct qdl_file flashmap;
	struct qdl_zip *zip = NULL;
	const char *dot;
	char *specifier;
	char *filename;
	char *tmp;
	char *base;
	int file_type = QDL_FILE_UNKNOWN;
	int ret;

	filename = qdl_split_specifier(arg, &specifier);
	if (!filename) {
		ux_err("failed to parse flash argument \"%s\" (expected <file> or <file>::<selector>)\n",
		       arg);
		return -1;
	}

	tmp = strdup(filename);
	if (!tmp)
		return -1;

	base = basename(tmp);
	dot = strrchr(base, '.');

	if (dot && !strcmp(dot, ".xml")) {
		file_type = QDL_FILE_CONTENTS;
	} else if (dot && !strcmp(dot, ".json")) {
		file_type = QDL_CMD_FLASH;
	} else {
		ret = qdl_zip_open(filename, &zip);
		if (!ret) {
			ret = qdl_file_open(zip, "flashmap.json", &flashmap);
			if (!ret) {
				qdl_file_close(&flashmap);
				file_type = QDL_CMD_FLASH;
			}
			qdl_zip_put(zip);
		}
	}
	free(tmp);

	switch (file_type) {
	case QDL_FILE_CONTENTS:
		ret = contents_load(firehose_ops, filename, specifier, images, incdir);
		break;
	case QDL_CMD_FLASH:
		ret = flashmap_load(firehose_ops, filename, specifier, images, incdir);
		break;
	default:
		ux_err("flash input must be contents.xml, flashmap.json, or a zip containing flashmap.json\n");
		if (!qdl_zip_supported())
			ux_err("note: this qdl was built without zip-container support\n");
		ret = -1;
		break;
	}

	free(filename);

	return ret;
}

static int qdl_cmd_reset(struct list_head *ops)
{
	struct firehose_op *reset_op = firehose_alloc_op(FIREHOSE_OP_RESET);

	if (!reset_op)
		return -1;

	list_append(ops, &reset_op->node);

	return 0;
}

static int qdl_create_zip(int argc, char **argv)
{
	struct sahara_image images[MAPPING_SZ] = {};
	struct list_head ops = LIST_INIT(ops);
	const char *zipfile = argv[1];
	char *specifier;
	char *filename;
	int ret;

	if (argc != 3) {
		print_usage(stderr);
		return 1;
	}

	ux_init();

	if (!qdl_zip_supported()) {
		ux_err("create-zip requires zip-container support, which this qdl was built without\n");
		return 1;
	}

	filename = qdl_split_specifier(argv[2], &specifier);
	if (!filename) {
		ux_err("failed to parse flash argument");
		return 1;
	}

	ret = contents_load(&ops, filename, specifier, images, NULL);
	if (ret < 0)
		goto out_free_filename;

	ret = zipper_write(zipfile, &ops, images);

	sahara_images_free(images, MAPPING_SZ);
	firehose_free_ops(&ops);

out_free_filename:
	free(filename);

	return ret ? 1 : 0;
}

static bool qdl_is_contents_xml(const char *filename)
{
	xmlNode *root;
	xmlDoc *doc;
	bool ret;

	doc = xmlReadFile(filename, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (!doc)
		return false;

	root = xmlDocGetRootElement(doc);
	ret = root && !xmlStrcmp(root->name, (xmlChar *)"contents");

	xmlFreeDoc(doc);

	return ret;
}

static int qdl_sahara_archive(int argc, char **argv)
{
	struct sahara_image images[MAPPING_SZ] = {};
	const char *archive;
	char *specifier;
	char *filename;
	int ret = 0;

	if (argc != 3) {
		print_usage(stderr);
		return 1;
	}

	ux_init();

	archive = argv[1];
	filename = qdl_split_specifier(argv[2], &specifier);
	if (!filename) {
		ux_err("failed to parse Sahara archive input \"%s\"\n", argv[2]);
		ret = -1;
		goto out_free_images;
	}

	if (qdl_is_contents_xml(filename)) {
		ret = contents_load_programmers(filename, specifier, images);
	} else {
		if (specifier) {
			ux_err("selectors can only be used with contents.xml inputs\n");
			ret = -1;
		} else {
			ret = decode_programmer(filename, images);
		}
	}
	free(filename);
	if (ret < 0)
		goto out_free_images;
	ret = 0;

	ret = sahara_archive_write(archive, images);

out_free_images:
	sahara_images_free(images, MAPPING_SZ);

	return ret ? 1 : 0;
}

static int qdl_determine_bootable(struct list_head *ops)
{
	struct firehose_op *op;
	bool multiple;
	int bootable;

	bootable = program_find_bootable_partition(ops, &multiple);
	if (bootable < 0) {
		ux_debug("no boot partition found\n");
		return 0;
	}

	if (multiple)
		ux_info("Multiple candidates for primary bootloader found, using partition %d\n",
			bootable);

	op = firehose_alloc_op(FIREHOSE_OP_SET_BOOTABLE);
	if (!op)
		return -1;

	op->partition = bootable;

	list_append(ops, &op->node);

	return 0;
}

/*
 * Walk the firehose op list and emit one hex line per
 * FIREHOSE_OP_GET_SHA256_DIGEST entry. firehose_run() fills op->digest;
 * formatting and printing live here so firehose.c stays out of the
 * user-facing output policy.
 *
 * If the request shipped but the device returned no digest
 * (digest_valid stayed false), surface that to the user instead of
 * silently skipping the region.
 */
static void print_sha256_results(struct list_head *ops)
{
	struct firehose_op *op;

	list_for_each_entry(op, ops, node) {
		char hex[SHA256_DIGEST_STRING_LENGTH];
		size_t i;

		if (op->type != FIREHOSE_OP_GET_SHA256_DIGEST)
			continue;

		if (!op->digest_valid) {
			ux_err("no sha256 digest returned for %s+0x%x\n",
			       op->start_sector, op->num_sectors);
			continue;
		}

		for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
			snprintf(hex + i * 2, 3, "%02x", op->digest[i]);
		hex[SHA256_DIGEST_STRING_LENGTH - 1] = '\0';

		printf("%s\n", hex);
		fflush(stdout);
	}
}

static int qdl_flash(int argc, char **argv)
{
	enum qdl_storage_type storage_type = QDL_STORAGE_UFS;
	struct sahara_image sahara_images[MAPPING_SZ] = {};
	struct list_head firehose_ops = LIST_INIT(firehose_ops);
	struct ufs_provisioning ufs;
	char *incdir = NULL;
	char *serial = NULL;
	const char *vip_generate_dir = NULL;
	const char *vip_table_path = NULL;
	int type;
	int ret;
	int opt;
	bool qdl_finalize_provisioning = false;
	bool allow_fusing = false;
	bool allow_missing = false;
	bool skip_reset = false;
	bool saw_file = false;
	bool saw_verb = false;
	long out_chunk_size = 0;
	unsigned int slot = UINT_MAX;
	struct qdl_device *qdl = NULL;
	enum QDL_DEVICE_TYPE qdl_dev_type = QDL_DEVICE_AUTO;
	enum qdl_skipblock_mode skipblock_mode = QDL_SKIPBLOCK_NONE;

	static struct option options[] = {
		{"debug", no_argument, 0, 'd'},
		{"version", no_argument, 0, 'v'},
		{"include", required_argument, 0, 'i'},
		{"finalize-provisioning", no_argument, 0, 'l'},
		{"out-chunk-size", required_argument, 0, 'u' },
		{"serial", required_argument, 0, 'S'},
		{"vip-table-path", required_argument, 0, 'D'},
		{"storage", required_argument, 0, 's'},
		{"allow-missing", no_argument, 0, 'f'},
		{"allow-fusing", no_argument, 0, 'c'},
		{"dry-run", no_argument, 0, 'n'},
		{"create-digests", required_argument, 0, 't'},
		{"slot", required_argument, 0, 'T'},
		{"skip-reset", no_argument, 0, 'R'},
		{"backend", required_argument, 0, OPT_BACKEND},
		{"skipblock", required_argument, 0, OPT_SKIPBLOCK},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	ufs_provisioning_init(&ufs);

	while ((opt = getopt_long(argc, argv, "dvi:lu:S:D:s:fcnt:T:Rh", options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			qdl_debug = true;
			break;
		case 'n':
			qdl_dev_type = QDL_DEVICE_SIM;
			break;
		case 't':
			vip_generate_dir = optarg;
			/* we also enforce dry-run mode */
			qdl_dev_type = QDL_DEVICE_SIM;
			break;
		case 'v':
			print_version();
			return 0;
		case 'f':
			allow_missing = true;
			break;
		case 'i':
			incdir = optarg;
			break;
		case 'l':
			qdl_finalize_provisioning = true;
			break;
		case 'c':
			allow_fusing = true;
			break;
		case 'u':
			out_chunk_size = strtol(optarg, NULL, 10);
			break;
		case 's':
			storage_type = decode_storage_type(optarg);
			if (storage_type == QDL_STORAGE_UNKNOWN)
				errx(1, "unknown storage type \"%s\"", optarg);
			break;
		case 'S':
			serial = optarg;
			break;
		case 'D':
			vip_table_path = optarg;
			break;
		case 'T':
			slot = (unsigned int)strtoul(optarg, NULL, 10);
			break;
		case 'R':
			skip_reset = true;
			break;
		case OPT_BACKEND:
			/*
			 * --dry-run / --create-digests already pinned the backend to
			 * QDL_DEVICE_SIM; honour that and ignore --backend in that case.
			 */
			if (qdl_dev_type != QDL_DEVICE_SIM &&
			    decode_backend(optarg, &qdl_dev_type) < 0)
				errx(1, "unknown backend \"%s\" (expected auto|usb|qud)", optarg);
			break;
		case OPT_SKIPBLOCK:
			if (!strcmp(optarg, "none"))
				skipblock_mode = QDL_SKIPBLOCK_NONE;
			else if (!strcmp(optarg, "sha256"))
				skipblock_mode = QDL_SKIPBLOCK_SHA256;
			else
				errx(1, "unknown --skipblock mode \"%s\", valid options are none and sha256",
				     optarg);
			break;
		case 'h':
			print_usage(stdout);
			return 0;
		default:
			print_usage(stderr);
			return 1;
		}
	}

	/* at least 2 non optional args required */
	if ((optind + 2) > argc) {
		print_usage(stderr);
		return 1;
	}

	qdl = qdl_init(qdl_dev_type);
	if (!qdl) {
		ret = -1;
		goto out_cleanup;
	}

	qdl->slot = slot;
	qdl->skipblock_mode = skipblock_mode;

	if (vip_table_path) {
		if (vip_generate_dir)
			errx(1, "VIP mode and VIP table generation can't be enabled together\n");
		ret = vip_transfer_init(qdl, vip_table_path);
		if (ret)
			errx(1, "VIP initialization failed\n");
	}

	if (out_chunk_size)
		qdl_set_out_chunk_size(qdl, out_chunk_size);

	if (vip_generate_dir) {
		ret = vip_gen_init(qdl, vip_generate_dir);
		if (ret)
			goto out_cleanup;
	}

	ux_init();

	if (qdl_debug)
		print_version();

	/*
	 * The programmer needs to either be selected explicitly or through the
	 * "flash" subcommand. Handling of "flash" happens in the loop below.
	 */
	if (strcmp(argv[optind], "flash")) {
		ret = decode_programmer(argv[optind++], sahara_images);
		if (ret < 0)
			goto out_cleanup;
	}

	do {
		type = detect_type(argv[optind]);
		if (type < 0 || type == QDL_FILE_UNKNOWN)
			errx(1, "failed to detect file type of %s\n", argv[optind]);

		/*
		 * The usage synopsis lists input XML files and command verbs
		 * (read/write/erase/sha256/flash/reset) as separate forms; they
		 * must not be mixed. Combining them once let a verb like "reset"
		 * be appended out of order relative to ops added after parsing.
		 * QDL_CMD_* follow the QDL_FILE_* values in the enum.
		 */
		if (type >= QDL_CMD_READ)
			saw_verb = true;
		else
			saw_file = true;

		if (saw_file && saw_verb)
			errx(1, "input XML files cannot be combined with command "
			     "verbs (read/write/erase/sha256/flash/reset)");

		switch (type) {
		case QDL_FILE_PATCH:
			ret = patch_load(&firehose_ops, argv[optind]);
			if (ret < 0)
				errx(1, "patch_load %s failed", argv[optind]);
			break;
		case QDL_FILE_PROGRAM:
			ret = program_load(&firehose_ops, argv[optind],
					   storage_type == QDL_STORAGE_NAND,
					   allow_missing, NULL, incdir);
			if (ret < 0)
				errx(1, "program_load %s failed", argv[optind]);

			if (!allow_fusing && program_is_sec_partition_flashed(&firehose_ops))
				errx(1, "secdata partition to be programmed, which can lead to irreversible"
					" changes. Allow explicitly with --allow-fusing parameter");
			break;
		case QDL_FILE_READ:
			ret = read_op_load(&firehose_ops, argv[optind], incdir);
			if (ret < 0)
				errx(1, "read_op_load %s failed", argv[optind]);
			break;
		case QDL_FILE_UFS:
			if (storage_type != QDL_STORAGE_UFS)
				errx(1, "attempting to load provisioning config when storage isn't \"ufs\"");

			ret = ufs_load(&ufs, argv[optind], qdl_finalize_provisioning);
			if (ret < 0)
				errx(1, "ufs_load %s failed", argv[optind]);
			break;
		case QDL_CMD_READ:
			if (optind + 2 >= argc)
				errx(1, "read command missing arguments");
			ret = read_cmd_add(&firehose_ops, argv[optind + 1], argv[optind + 2]);
			if (ret < 0)
				errx(1, "failed to add read command");
			optind += 2;
			break;
		case QDL_CMD_WRITE:
			if (optind + 2 >= argc)
				errx(1, "write command missing arguments");
			ret = program_cmd_add(&firehose_ops, argv[optind + 1], argv[optind + 2]);
			if (ret < 0)
				errx(1, "failed to add write command");
			optind += 2;
			break;
		case QDL_CMD_ERASE:
			if (optind + 1 >= argc)
				errx(1, "erase command missing address");
			ret = erase_cmd_add(&firehose_ops, argv[optind + 1]);
			if (ret < 0)
				errx(1, "failed to add erase command");
			optind += 1;
			break;
		case QDL_CMD_SHA256:
			if (optind + 1 >= argc)
				errx(1, "sha256 command missing address");
			ret = sha256_cmd_add(&firehose_ops, argv[optind + 1]);
			if (ret < 0)
				errx(1, "failed to add sha256 command");
			optind += 1;
			break;
		case QDL_CMD_FLASH:
			if (optind + 1 >= argc)
				errx(1, "flash command missing operands");
			ret = qdl_cmd_flash(&firehose_ops, argv[optind + 1], incdir, sahara_images);
			if (ret < 0)
				goto out_cleanup;
			optind += 1;
			break;
		case QDL_CMD_RESET:
			/* Do no allocate two reset commands */
			skip_reset = true;
			/* Stop processing chained commands */
			optind = argc;
			ret = qdl_cmd_reset(&firehose_ops);
			if (ret < 0)
				goto out_cleanup;
			break;
		default:
			errx(1, "%s type not yet supported", argv[optind]);
			break;
		}
	} while (++optind < argc);

	ret = qdl_ensure_configured(&firehose_ops, storage_type);
	if (ret < 0)
		goto out_cleanup;

	ret = qdl_determine_bootable(&firehose_ops);
	if (ret)
		goto out_cleanup;

	/*
	 * Reset is the last operation in any flashing run, modelled as a regular
	 * firehose op so callers can compose it like any other. Skip the append
	 * to leave the programmer alive across qdl invocations.
	 */
	if (!skip_reset) {
		ret = qdl_cmd_reset(&firehose_ops);
		if (ret < 0)
			goto out_cleanup;
	}

	ret = qdl_open(qdl, serial);
	if (ret)
		goto out_cleanup;

	ret = sahara_run(qdl, sahara_images, NULL, NULL);
	if (ret < 0)
		goto out_cleanup;

	if (ufs_need_provisioning(&ufs))
		ret = firehose_provision(qdl, &ufs, skip_reset);
	else
		ret = firehose_run(qdl, &firehose_ops);
	if (ret < 0)
		goto out_cleanup;

	print_sha256_results(&firehose_ops);

out_cleanup:
	if (qdl) {
		if (vip_generate_dir)
			vip_gen_finalize(qdl);

		qdl_close(qdl);
	}

	sahara_images_free(sahara_images, MAPPING_SZ);

	firehose_free_ops(&firehose_ops);

	ufs_provisioning_cleanup(&ufs);

	if (qdl) {
		if (qdl->vip_data.state != VIP_DISABLED)
			vip_transfer_deinit(qdl);

		qdl_deinit(qdl);
	}

	return !!ret;
}

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "list"))
			return qdl_list(stdout);
		if (!strcmp(argv[i], "ramdump"))
			return qdl_ramdump(argc - i, argv + i);
		if (!strcmp(argv[i], "chipinfo"))
			return qdl_sahara_cmd(argc - i, argv + i, sahara_chipinfo);
		if (!strcmp(argv[i], "reset"))
			return qdl_sahara_cmd(argc - i, argv + i, qdl_reset_run);
		if (!strcmp(argv[i], "ks"))
			return qdl_ks(argc - i, argv + i);
		if (!strcmp(argv[i], "create-zip"))
			return qdl_create_zip(argc - i, argv + i);
		if (!strcmp(argv[i], "create-sahara-archive"))
			return qdl_sahara_archive(argc - i, argv + i);
		if (argv[i][0] != '-')
			break;
	}

	return qdl_flash(argc, argv);
}
