#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "qdl.h"

static struct qdl_device qdl;

bool qdl_debug;

static void print_usage(void)
{
	extern const char *__progname;
	fprintf(stderr,
		"%s [--debug] [--serial <NUM>] [--port <sahara dev_node>] -s <id:file path> ...\n",
		__progname);
	fprintf(stderr,
		" -p                   --port                      Sahara device node to use\n"
		" -s <id:file path>    --sahara <id:file path>     Sahara protocol file mapping\n"
		"\n"
		"One or more -s instances are required\n"
		"\n"
		"Example: \n"
		"ks -p /dev/mhi0_QAIC_SAHARA -s 1:/opt/qti-aic/firmware/fw1.bin -s 2:/opt/qti-aic/firmware/fw2.bin\n");
}

int main(int argc, char **argv)
{
	bool found_mapping = false;
	char *dev_node = NULL;
	char *serial = NULL;
	long file_id;
	char *colon;
	int opt;
	int ret;

	static struct option options[] = {
		{"debug", no_argument, 0, 'd'},
		{"version", no_argument, 0, 'v'},
		{"port", required_argument, 0, 'p'},
		{"sahara", required_argument, 0, 's'},
		{"serial", required_argument, 0, 'S'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "dvp:s:", options, NULL )) != -1) {
		switch (opt) {
		case 'd':
			qdl_debug = true;
			break;
		case 'v':
			print_version();
			return 0;
		case 'p':
			dev_node = optarg;
			printf("Using port - %s\n", dev_node);
			break;
		case 's':
			found_mapping = true;
			file_id = strtol(optarg, NULL, 10);
			if (file_id < 0 || file_id >= MAPPING_SZ)
				errx(1, "ID:%ld has to be in range of 0 - %d\n", file_id, MAPPING_SZ - 1);

			colon = strchr(optarg, ':');
			if (!colon)
				errx(1, "Sahara mapping requires ID and file path to be divided by a colon");

			qdl.mappings[file_id] = &optarg[colon - optarg + 1];
			printf("Created mapping ID:%ld File:%s\n", file_id, qdl.mappings[file_id]);
			break;
		case 'S':
			serial = optarg;
			break;
		default:
			print_usage();
			return 1;
		}
	}

	// -s is required
	if (!found_mapping) {
		print_usage();
		return 1;
	}

	if (qdl_debug)
		print_version();

	if (dev_node) {
		qdl.fd = open(dev_node, O_RDWR);
		if (qdl.fd < 0) {
			ret = 0;
			printf("Unable to open %s\n", dev_node);
			goto out_cleanup;
		}
	}
	else {
		ret = qdl_open(&qdl, serial);
		if (ret) {
			printf("Failed to find edl device\n");
			goto out_cleanup;
		}
	}


	ret = sahara_run(&qdl, qdl.mappings, false, NULL, NULL);
	if (ret < 0)
		goto out_cleanup;

out_cleanup:
	if (dev_node)
		close(qdl.fd);
	else
		qdl_close(&qdl);

	return !!ret;
}
