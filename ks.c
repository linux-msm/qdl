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

int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout)
{
	return read(qdl->fd, buf, len);
}

int qdl_write(struct qdl_device *qdl, const void *buf, size_t len)
{

	return write(qdl->fd, buf, len);
}

static void print_usage(void)
{
	extern const char *__progname;
	fprintf(stderr,
		"%s -p <sahara dev_node> -s <id:file path> ...\n",
		__progname);
	fprintf(stderr,
		" -p                   --port                      Sahara device node to use\n"
		" -s <id:file path>    --sahara <id:file path>     Sahara protocol file mapping\n"
		"\n"
		"One -p instance is required.  One or more -s instances are required.\n"
		"\n"
		"Example: \n"
		"ks -p /dev/mhi0_QAIC_SAHARA -s 1:/opt/qti-aic/firmware/fw1.bin -s 2:/opt/qti-aic/firmware/fw2.bin\n");
}

int main(int argc, char **argv)
{
	bool found_mapping = false;
	char *dev_node = NULL;
	long file_id;
	char *colon;
	int opt;
	int ret;

	static struct option options[] = {
		{"port", required_argument, 0, 'p'},
		{"sahara", required_argument, 0, 's'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "p:s:", options, NULL )) != -1) {
		switch (opt) {
		case 'p':
			dev_node = optarg;
			printf("Using port - %s\n", dev_node);
			break;
		case 's':
			found_mapping = true;
			file_id = strtol(optarg, NULL, 10);
			if (file_id < 0) {
				print_usage();
				return 1;
			}
			if (file_id >= MAPPING_SZ) {
				fprintf(stderr,
					"ID:%ld exceeds the max value of %d\n",
					file_id,
					MAPPING_SZ - 1);
				return 1;
			}
			colon = strchr(optarg, ':');
			if (!colon) {
				print_usage();
				return 1;
			}
			qdl.mappings[file_id] = &optarg[colon - optarg + 1];
			printf("Created mapping ID:%ld File:%s\n", file_id, qdl.mappings[file_id]);
			break;
		default:
			print_usage();
			return 1;
		}
	}

	// -p and -s is required
	if (!dev_node || !found_mapping) {
		print_usage();
		return 1;
	}

	qdl.fd = open(dev_node, O_RDWR);
	if (qdl.fd < 0) {
		fprintf(stderr, "Unable to open %s\n", dev_node);
		return 1;
	}

	ret = sahara_run(&qdl, qdl.mappings, false);
	if (ret < 0)
		return 1;

	return 0;
}
