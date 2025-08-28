// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * All rights reserved.
 */
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <unistd.h>

#include "qdl.h"
#include "patch.h"
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

static void print_usage(FILE *out)
{
	extern const char *__progname;

	fprintf(out, "Usage: %s [options] <prog.mbn> (<program-xml> | <patch-xml> | <read-xml>)...\n", __progname);
	fprintf(out, "       %s [options] <prog.mbn> ((read | write) <address> <binary>)...\n", __progname);
	fprintf(out, " -d, --debug\t\t\tPrint detailed debug info\n");
	fprintf(out, " -v, --version\t\t\tPrint the current version and exit\n");
	fprintf(out, " -n, --dry-run\t\t\tDry run execution, no device reading or flashing\n");
	fprintf(out, " -f, --allow-missing\t\tAllow skipping of missing files during flashing\n");
	fprintf(out, " -s, --storage=T\t\tSet target storage type T: <emmc|nand|ufs>\n");
	fprintf(out, " -l, --finalize-provisioning\tProvision the target storage\n");
	fprintf(out, " -i, --include=T\t\tSet an optional folder T to search for files\n");
	fprintf(out, " -S, --serial=T\t\t\tSelect target by serial number T (e.g. <0AA94EFD>)\n");
	fprintf(out, " -u, --out-chunk-size=T\t\tOverride chunk size for transaction with T\n");
	fprintf(out, " -t, --create-digests=T\t\tGenerate table of digests in the T folder\n");
	fprintf(out, " -D, --vip-table-path=T\t\tUse digest tables in the T folder for VIP\n");
	fprintf(out, " -h, --help\t\t\tPrint this usage info\n");
	fprintf(out, " <program-xml>\txml file containing <program> or <erase> directives\n");
	fprintf(out, " <patch-xml>\txml file containing <patch> directives\n");
	fprintf(out, " <read-xml>\txml file containing <read> directives\n");
	fprintf(out, " <address>\tdisk address specifier, can be one of <P>, <P/S>, <P/S+L>, <name>, or\n");
	fprintf(out, "          \t<P/name>, to specify a physical partition number P, a starting sector\n");
	fprintf(out, "          \tnumber S, the number of sectors to follow L, or partition by \"name\"\n");
	fprintf(out, "\n");
	fprintf(out, "Example: %s prog_firehose_ddr.elf rawprogram*.xml patch*.xml\n", __progname);
}

int main(int argc, char **argv)
{
	char *prog_mbn, *storage = "ufs";
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
	long out_chunk_size = 0;
	struct qdl_device *qdl = NULL;
	enum QDL_DEVICE_TYPE qdl_dev_type = QDL_DEVICE_USB;

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
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "dvi:lu:S:D:s:fcnt:h", options, NULL)) != -1) {
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
			storage = optarg;
			break;
		case 'S':
			serial = optarg;
			break;
		case 'D':
			vip_table_path = optarg;
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

	prog_mbn = argv[optind++];

	do {
		type = detect_type(argv[optind]);
		if (type < 0 || type == QDL_FILE_UNKNOWN)
			errx(1, "failed to detect file type of %s\n", argv[optind]);

		switch (type) {
		case QDL_FILE_PATCH:
			ret = patch_load(argv[optind]);
			if (ret < 0)
				errx(1, "patch_load %s failed", argv[optind]);
			break;
		case QDL_FILE_PROGRAM:
			ret = program_load(argv[optind], !strcmp(storage, "nand"), allow_missing, incdir);
			if (ret < 0)
				errx(1, "program_load %s failed", argv[optind]);

			if (!allow_fusing && program_is_sec_partition_flashed())
				errx(1, "secdata partition to be programmed, which can lead to irreversible"
					" changes. Allow explicitly with --allow-fusing parameter");
			break;
		case QDL_FILE_READ:
			ret = read_op_load(argv[optind], incdir);
			if (ret < 0)
				errx(1, "read_op_load %s failed", argv[optind]);
			break;
		case QDL_FILE_UFS:
			ret = ufs_load(argv[optind], qdl_finalize_provisioning);
			if (ret < 0)
				errx(1, "ufs_load %s failed", argv[optind]);
			break;
		case QDL_CMD_READ:
			if (optind + 2 >= argc)
				errx(1, "read command missing arguments");
			ret = read_cmd_add(argv[optind + 1], argv[optind + 2]);
			if (ret < 0)
				errx(1, "failed to add read command");
			optind += 2;
			break;
		case QDL_CMD_WRITE:
			if (optind + 2 >= argc)
				errx(1, "write command missing arguments");
			ret = program_cmd_add(argv[optind + 1], argv[optind + 2]);
			if (ret < 0)
				errx(1, "failed to add write command");
			optind += 2;
			break;
		default:
			errx(1, "%s type not yet supported", argv[optind]);
			break;
		}
	} while (++optind < argc);

	ret = qdl_open(qdl, serial);
	if (ret)
		goto out_cleanup;

	qdl->mappings[0] = prog_mbn;
	ret = sahara_run(qdl, qdl->mappings, true, NULL, NULL);
	if (ret < 0)
		goto out_cleanup;

	ret = firehose_run(qdl, storage);
	if (ret < 0)
		goto out_cleanup;

out_cleanup:
	if (vip_generate_dir)
		vip_gen_finalize(qdl);

	qdl_close(qdl);
	free_programs();
	free_patches();

	if (qdl->vip_data.state != VIP_DISABLED)
		vip_transfer_deinit(qdl);

	qdl_deinit(qdl);

	return !!ret;
}
