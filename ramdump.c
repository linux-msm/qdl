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
		"%s [--debug] [-o <ramdump-path>]\n",
		__progname);
	exit(1);
}

int main(int argc, char **argv)
{
	struct qdl_device qdl;
	char *ramdump_path = ".";
	int ret;
	int opt;

	static struct option options[] = {
		{"debug", no_argument, 0, 'd'},
		{"output", required_argument, 0, 'o'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "do:", options, NULL )) != -1) {
		switch (opt) {
		case 'd':
			qdl_debug = true;
			break;
		case 'o':
			ramdump_path = optarg;
			break;
		default:
			print_usage();
		}
	}

	if (optind != argc)
		print_usage();

	ret = qdl_open(&qdl);
	if (ret)
		return 1;

	ret = sahara_run(&qdl, NULL, true, ramdump_path);
	if (ret < 0)
		return 1;

	return 0;
}
