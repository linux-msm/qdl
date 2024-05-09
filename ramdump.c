#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "qdl.h"

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
	struct qdl_device qdl;
	char *ramdump_path = ".";
	char *filter = NULL;
	char *serial = NULL;
	int ret;
	int opt;

	static struct option options[] = {
		{"debug", no_argument, 0, 'd'},
		{"output", required_argument, 0, 'o'},
		{"serial", required_argument, 0, 'S'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "do:S:", options, NULL )) != -1) {
		switch (opt) {
		case 'd':
			qdl_debug = true;
			break;
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

	ret = qdl_open(&qdl, serial);
	if (ret)
		return 1;

	ret = sahara_run(&qdl, NULL, true, ramdump_path, filter);
	if (ret < 0)
		return 1;

	return 0;
}
