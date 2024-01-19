/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "qdl.h"
#include "patch.h"
#include "ufs.h"

#define MAX_USBFS_BULK_SIZE	(16*1024)

enum {
	QDL_FILE_UNKNOWN,
	QDL_FILE_PATCH,
	QDL_FILE_PROGRAM,
	QDL_FILE_READ,
	QDL_FILE_UFS,
	QDL_FILE_CONTENTS,
};

bool qdl_debug;
static struct qdl_device qdl;

static int detect_type(const char *xml_file)
{
	xmlNode *root;
	xmlDoc *doc;
	xmlNode *node;
	int type = QDL_FILE_UNKNOWN;

	doc = xmlReadFile(xml_file, NULL, 0);
	if (!doc) {
		fprintf(stderr, "[PATCH] failed to parse %s\n", xml_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	if (!xmlStrcmp(root->name, (xmlChar*)"patches")) {
		type = QDL_FILE_PATCH;
	} else if (!xmlStrcmp(root->name, (xmlChar*)"data")) {
		for (node = root->children; node ; node = node->next) {
			if (node->type != XML_ELEMENT_NODE)
				continue;
			if (!xmlStrcmp(node->name, (xmlChar*)"program")) {
				type = QDL_FILE_PROGRAM;
				break;
			}
			if (!xmlStrcmp(node->name, (xmlChar*)"read")) {
				type = QDL_FILE_READ;
				break;
			}
			if (!xmlStrcmp(node->name, (xmlChar*)"ufs")) {
				type = QDL_FILE_UFS;
				break;
			}
		}
	} else if (!xmlStrcmp(root->name, (xmlChar*)"contents")) {
		type = QDL_FILE_CONTENTS;
	}

	xmlFreeDoc(doc);

	return type;
}

static void print_usage(void)
{
	extern const char *__progname;
	fprintf(stderr,
		"%s [--debug] [--storage <emmc|nand|ufs>] [--finalize-provisioning] [--include <PATH>] <prog.mbn> [<program> <patch> ...]\n",
		__progname);
}

int main(int argc, char **argv)
{
	char *prog_mbn, *storage="ufs";
	char *incdir = NULL;
	char *serial = NULL;
	int type;
	int ret;
	int opt;
	bool qdl_finalize_provisioning = false;


	static struct option options[] = {
		{"debug", no_argument, 0, 'd'},
		{"include", required_argument, 0, 'i'},
		{"finalize-provisioning", no_argument, 0, 'l'},
		{"serial", required_argument, 0, 'S'},
		{"storage", required_argument, 0, 's'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "di:S:", options, NULL )) != -1) {
		switch (opt) {
		case 'd':
			qdl_debug = true;
			break;
		case 'i':
			incdir = optarg;
			break;
		case 'l':
			qdl_finalize_provisioning = true;
			break;
		case 's':
			storage = optarg;
			break;
		case 'S':
			serial = optarg;
			break;
		default:
			print_usage();
			return 1;
		}
	}

	/* at least 2 non optional args required */
	if ((optind + 2) > argc) {
		print_usage();
		return 1;
	}

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
			ret = program_load(argv[optind], !strcmp(storage, "nand"));
			if (ret < 0)
				errx(1, "program_load %s failed", argv[optind]);
			break;
		case QDL_FILE_READ:
			ret = read_op_load(argv[optind]);
			if (ret < 0)
				errx(1, "read_op_load %s failed", argv[optind]);
			break;
		case QDL_FILE_UFS:
			ret = ufs_load(argv[optind],qdl_finalize_provisioning);
			if (ret < 0)
				errx(1, "ufs_load %s failed", argv[optind]);
			break;
		default:
			errx(1, "%s type not yet supported", argv[optind]);
			break;
		}
	} while (++optind < argc);

	ret = qdl_open(&qdl, serial);
	if (ret)
		return 1;

	qdl.mappings[0] = prog_mbn;
	ret = sahara_run(&qdl, qdl.mappings, true, NULL, NULL);
	if (ret < 0)
		return 1;

	ret = firehose_run(&qdl, incdir, storage);
	if (ret < 0)
		return 1;

	return 0;
}
