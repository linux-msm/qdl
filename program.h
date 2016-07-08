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
	bool readback;
	unsigned size;
	bool sparse;
	const char *start_bytes;
	const char *start_sector;

	struct program *next;
};

int program_load(const char *program_file);
int program_execute(int usbfd, void (*apply)(int usbfd, struct program *program, int fd));

#endif
