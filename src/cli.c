// SPDX-License-Identifier: BSD-3-Clause
/* Command-line front-end helpers: usage text and flashing-run options. */
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qdl.h"
#include "oscompat.h"
#include "cli.h"

void print_usage(FILE *out)
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

/*
 * Parse the flashing options into @opts and leave optind pointing at the
 * first positional argument. Fatal value errors (unknown storage/backend/
 * skipblock) still errx() out; the return value only distinguishes the
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

	while ((opt = getopt_long(argc, argv, "dvi:lu:S:D:s:fcnt:T:Rh", options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			qdl_debug = true;
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
				errx(1, "unknown storage type \"%s\"", optarg);
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
				errx(1, "unknown backend \"%s\" (expected auto|usb|qud)", optarg);
			break;
		case OPT_SKIPBLOCK:
			if (!strcmp(optarg, "none"))
				opts->skipblock_mode = QDL_SKIPBLOCK_NONE;
			else if (!strcmp(optarg, "sha256"))
				opts->skipblock_mode = QDL_SKIPBLOCK_SHA256;
			else
				errx(1, "unknown --skipblock mode \"%s\", valid options are none and sha256",
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
