#ifndef __PROGRAM_H__
#define __PROGRAM_H__

#include <stdbool.h>

struct program {
	unsigned sector_size;
	unsigned file_offset;
	const char *filename;
	const char *label;
	unsigned num_sectors;
	unsigned partition;
	unsigned size;
	bool sparse;
	const char *start_bytes;
	const char *start_sector;

	struct program *next;
};

int program_load(const char *program_file);
int program_execute(int usbfd, int (*apply)(int usbfd, struct program *program, int fd));
int program_find_bootable_partition(void);

#endif
