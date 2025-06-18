#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "qdl.h"

#ifdef _WIN32
const char *__progname = "ramdump";
#endif

bool qdl_debug;

static void print_usage(void)
{
	extern const char *__progname;

	fprintf(stderr,
		"%s [--debug] [-o <ramdump-path>] [segment-filter,...]\n",
		__progname);
	exit(1);
}

int main(int argc, char **argv)
{
	struct qdl_device *qdl;

	qdl = qdl_init(QDL_DEVICE_USB);
	if (!qdl)
		return 1;

	char *ramdump_path = ".";
	char *filter = NULL;
	char *serial = NULL;
	int ret = 0;
	int opt;

	static struct option options[] = {
		{"debug", no_argument, 0, 'd'},
		{"version", no_argument, 0, 'v'},
		{"output", required_argument, 0, 'o'},
		{"serial", required_argument, 0, 'S'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "dvo:S:", options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			qdl_debug = true;
			break;
		case 'v':
			print_version();
			ret = 0;
			goto out_cleanup;
		case 'o':
			ramdump_path = optarg;
			break;
		case 'S':
			serial = optarg;
			break;
		default:
			print_usage();
		}
	}

	if (optind < argc)
		filter = argv[optind++];

	if (optind != argc)
		print_usage();

	if (qdl_debug)
		print_version();

	ret = qdl_open(qdl, serial);
	if (ret) {
		ret = 1;
		goto out_cleanup;
	}

	ret = sahara_run(qdl, NULL, true, ramdump_path, filter);
	if (ret < 0) {
		ret = 1;
		goto out_cleanup;
	}

out_cleanup:
	qdl_close(qdl);
	qdl_deinit(qdl);

	return ret;
}
