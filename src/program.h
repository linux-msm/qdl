#ifndef __PROGRAM_H__
#define __PROGRAM_H__

#include <stdbool.h>
#include "qdl.h"

struct program {
	unsigned pages_per_block;
	unsigned sector_size;
	unsigned file_offset;
	const char *filename;
	const char *label;
	unsigned num_sectors;
	unsigned partition;
	const char *start_sector;
	unsigned last_sector;

	bool is_nand;
	bool is_erase;

	struct program *next;
};

int program_load(const char *program_file, bool is_nand);
int program_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program, int fd),
		    const char *incdir);
int erase_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program));
int program_find_bootable_partition(void);

#endif
