#ifndef __PROGRAM_H__
#define __PROGRAM_H__

#include <libxml/tree.h>
#include <stdbool.h>
#include "qdl.h"

struct program {
	unsigned sector_size;
	unsigned file_offset;
	xmlChar *filename;
	xmlChar *label;
	unsigned num_sectors;
	unsigned partition;
	bool is_sparse;
	xmlChar *start_sector;

	struct program *next;
};

int program_load(const char *program_file);
int program_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program, int fd),
		    const char *incdir);
int program_find_bootable_partition(void);
void program_unload(void);

#endif
