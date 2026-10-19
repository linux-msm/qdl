// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
/* Command-line front-end helpers: usage text and flashing-run options. */
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qdl.h"
#include "oscompat.h"
#include "cli.h"
#include "version.h"

/* Print one option or argument description, highlighting its @name */
static void usage_opt(FILE *out, const char *indent, const char *name, const char *desc)
{
	fprintf(out, "%s%s%s%s%s\n", indent, ux_style(out, UX_STYLE_OPTION), name,
		ux_style(out, UX_STYLE_RESET), desc);
}

void print_usage(FILE *out)
{
	fprintf(out, "%sUsage:%s %s [options] <prog.mbn> (<program-xml> | <patch-xml> | <read-xml>)...\n",
		ux_style(out, UX_STYLE_HEADING), ux_style(out, UX_STYLE_RESET), ux_progname());
	fprintf(out, "       %s [options] <prog.mbn> ((read | write) <address> <binary>)...\n", ux_progname());
	fprintf(out, "       %s [options] <prog.mbn> (erase <address>)...\n", ux_progname());
	fprintf(out, "       %s [options] <prog.mbn> (sha256 <address>)...\n", ux_progname());
	fprintf(out, "       %s [options] <prog.mbn> (reset)\n", ux_progname());
	fprintf(out, "       %s list\n", ux_progname());
	fprintf(out, "       %s chipinfo\n", ux_progname());
	fprintf(out, "       %s reset\n", ux_progname());
	fprintf(out, "       %s ramdump [--debug] [-o <ramdump-path>] [<segment-filter>,...]\n", ux_progname());
	fprintf(out, "       %s ks [-p <sahara-dev-node> | --serial=T] -s <id:file-path>...\n", ux_progname());
	fprintf(out, "       %s flash (<flashmap>[::specifier] | <contents>[::<specifier>])\n", ux_progname());
	fprintf(out, "       %s create-zip <zipfile> <contents>[::<specifier>]\n", ux_progname());
	fprintf(out, "       %s create-sahara-archive <archive.bin> "
		"(<id:file>[,<id:file>...] | <sahara.xml> | <contents.xml>[::<specifier>])\n",
		ux_progname());
	usage_opt(out, " ", "-d, --debug", "\t\t\tPrint detailed debug info");
	usage_opt(out, " ", "-q, --quiet", "\t\t\tPrint only the summary, warnings and errors");
	usage_opt(out, "     ", "--color[=W]", "\t\tColorize output W: <auto|always|never> (default: auto)");
	usage_opt(out, " ", "-v, --version", "\t\t\tPrint the current version and exit");
	usage_opt(out, " ", "-n, --dry-run", "\t\t\tDry run execution, no device reading or flashing");
	usage_opt(out, " ", "-f, --allow-missing", "\t\tAllow skipping of missing files during flashing");
	usage_opt(out, " ", "-s, --storage=T", "\t\tSet target storage type T: <emmc|nand|nvme|spinor|ufs>");
	usage_opt(out, " ", "-l, --finalize-provisioning", "\tProvision the target storage");
	usage_opt(out, " ", "-i, --include=T", "\t\tSet an optional folder T to search for files");
	usage_opt(out, " ", "-S, --serial=T", "\t\t\tSelect target by serial number T (e.g. <0AA94EFD>)");
	usage_opt(out, " ", "-u, --out-chunk-size=T", "\t\tOverride chunk size for transaction with T");
	usage_opt(out, " ", "-t, --create-digests=T", "\t\tGenerate table of digests in the T folder");
	usage_opt(out, " ", "-T, --slot=T", "\t\t\tSet slot number T for multiple storage devices");
	usage_opt(out, " ", "-D, --vip-table-path=T", "\t\tUse digest tables in the T folder for VIP");
	usage_opt(out, " ", "-R, --skip-reset",
		  "\t\tDo not send the reset command after flashing completes");
	usage_opt(out, "     ", "--backend=B", "\t\tSelect device backend B: <auto|usb|qud> (default: auto)");
	usage_opt(out, "     ", "--skipblock=M",
		  "\t\tUse readback mechanism M to skip <program> entries already on flash;");
	fprintf(out, "                 \t\tM: <none|sha256> (default: none)\n");
	usage_opt(out, " ", "-h, --help", "\t\t\tPrint this usage info");
	usage_opt(out, " ", "<program-xml>", "\t\txml file containing <program> or <erase> directives");
	usage_opt(out, " ", "<patch-xml>", "\t\txml file containing <patch> directives");
	usage_opt(out, " ", "<read-xml>", "\t\txml file containing <read> directives");
	usage_opt(out, " ", "<address>", "\t\tdisk address specifier, can be one of <P>, <P/S>, <P/S+L>, <name>, or");
	fprintf(out, "          \t\t<P/name>, to specify a physical partition number P, a starting sector\n");
	fprintf(out, "          \t\tnumber S, the number of sectors to follow L, or partition by \"name\"\n");
	usage_opt(out, " ", "<ramdump-path>", "\t\tpath where ramdump should stored");
	usage_opt(out, " ", "<segment-filter>", "\toptional glob-pattern to select which segments to ramdump");
	usage_opt(out, " ", "<sahara-dev-node>", "\tSahara device node, e.g. /dev/mhi0_QAIC_SAHARA (ks);");
	fprintf(out, "                 \tomit to use the selected device backend (ks)\n");
	usage_opt(out, " ", "<id:file-path>", "\t\tmap a Sahara image id to a host file, repeatable (ks)");
	usage_opt(out, " ", "<flashmap>", "\tflashmap JSON file, or ZIP archive with flashmap.json");
	usage_opt(out, " ", "<contents>", "\tcontents XML file");
	usage_opt(out, " ", "<archive.bin>", "\tSahara programmer archive to create");
	usage_opt(out, " ", "<specifier>",
		  "\tcomma-separated list of specifiers, such as storage type, layout, and flavors");
	fprintf(out, "\n");
	fprintf(out, "%sExample:%s %s prog_firehose_ddr.elf rawprogram*.xml patch*.xml\n",
		ux_style(out, UX_STYLE_HEADING), ux_style(out, UX_STYLE_RESET), ux_progname());
	fprintf(out, "         %s flash contents.xml::ufs,spinor/safe_rtos\n", ux_progname());
	fprintf(out, "         %s flash installer.zip::layout1/ufs\n", ux_progname());
}

/*
 * qdl_take_color_option() - strip --color[=WHEN] from @argv and apply it
 *
 * Done before dispatch, as every subcommand has its own option parser.
 * Arguments after "--" are left alone; the last occurrence wins.
 *
 * Return: 0, or -1 if WHEN is invalid.
 */
int qdl_take_color_option(int *argc, char **argv)
{
	enum ux_color_mode mode;
	const char *value;
	int out = 1;
	int i;

	/* Keep an empty argv empty, argv[1] is past its end */
	if (*argc < 1)
		return 0;

	for (i = 1; i < *argc; i++) {
		if (!strcmp(argv[i], "--")) {
			while (i < *argc)
				argv[out++] = argv[i++];
			break;
		}

		if (!strcmp(argv[i], "--color")) {
			value = NULL;
		} else if (!strncmp(argv[i], "--color=", 8)) {
			value = argv[i] + 8;
		} else {
			argv[out++] = argv[i];
			continue;
		}

		if (decode_color_mode(value, &mode) < 0) {
			ux_warnx("unknown color mode \"%s\" (expected auto|always|never)", value);
			return -1;
		}

		ux_set_color_mode(mode);
	}

	*argc = out;
	argv[out] = NULL;

	return 0;
}

void print_version(void)
{
	fprintf(stdout, "%s version %s\n", ux_progname(), VERSION);
}

/*
 * Parse the flashing options into @opts and leave optind pointing at the
 * first positional argument. Fatal value errors (unknown storage/backend/
 * skipblock) still ux_diex() out; the return value only distinguishes the
 * normal, help/version and usage-error cases.
 */
int qdl_parse_args(int argc, char **argv, struct qdl_opts *opts)
{
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
		{"quiet", no_argument, 0, 'q'},
		{"backend", required_argument, 0, OPT_BACKEND},
		{"skipblock", required_argument, 0, OPT_SKIPBLOCK},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
	int opt;

	*opts = (struct qdl_opts){
		.storage_type = QDL_STORAGE_UFS,
		.dev_type = QDL_DEVICE_AUTO,
		.skipblock_mode = QDL_SKIPBLOCK_NONE,
		.slot = UINT_MAX,
	};

	while ((opt = getopt_long(argc, argv, "dvi:lu:S:D:s:fcnt:T:Rqh", options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			qdl_debug = true;
			break;
		case 'q':
			ux_set_quiet(true);
			break;
		case 'n':
			opts->dev_type = QDL_DEVICE_SIM;
			break;
		case 't':
			opts->vip_generate_dir = optarg;
			/* we also enforce dry-run mode */
			opts->dev_type = QDL_DEVICE_SIM;
			break;
		case 'v':
			print_version();
			return QDL_ARGS_DONE;
		case 'f':
			opts->allow_missing = true;
			break;
		case 'i':
			opts->incdir = optarg;
			break;
		case 'l':
			opts->finalize_provisioning = true;
			break;
		case 'c':
			opts->allow_fusing = true;
			break;
		case 'u':
			opts->out_chunk_size = strtol(optarg, NULL, 10);
			break;
		case 's':
			opts->storage_type = decode_storage_type(optarg);
			if (opts->storage_type == QDL_STORAGE_UNKNOWN)
				ux_diex(1, "unknown storage type \"%s\"", optarg);
			break;
		case 'S':
			opts->serial = optarg;
			break;
		case 'D':
			opts->vip_table_path = optarg;
			break;
		case 'T':
			opts->slot = (unsigned int)strtoul(optarg, NULL, 10);
			break;
		case 'R':
			opts->skip_reset = true;
			break;
		case OPT_BACKEND:
			/*
			 * --dry-run / --create-digests already pinned the backend to
			 * QDL_DEVICE_SIM; honour that and ignore --backend in that case.
			 */
			if (opts->dev_type != QDL_DEVICE_SIM &&
			    decode_backend(optarg, &opts->dev_type) < 0)
				ux_diex(1, "unknown backend \"%s\" (expected auto|usb|qud)", optarg);
			break;
		case OPT_SKIPBLOCK:
			if (!strcmp(optarg, "none"))
				opts->skipblock_mode = QDL_SKIPBLOCK_NONE;
			else if (!strcmp(optarg, "sha256"))
				opts->skipblock_mode = QDL_SKIPBLOCK_SHA256;
			else
				ux_diex(1, "unknown --skipblock mode \"%s\", valid options are none and sha256",
					optarg);
			break;
		case 'h':
			print_usage(stdout);
			return QDL_ARGS_DONE;
		default:
			print_usage(stderr);
			return QDL_ARGS_ERROR;
		}
	}

	/* at least 2 non optional args required */
	if ((optind + 2) > argc) {
		print_usage(stderr);
		return QDL_ARGS_ERROR;
	}

	return QDL_ARGS_OK;
}
