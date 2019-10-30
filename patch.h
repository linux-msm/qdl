#ifndef __PATCH_H__
#define __PATCH_H__

#include <libxml/tree.h>

struct qdl_device;

struct patch {
	unsigned sector_size;
	unsigned byte_offset;
	xmlChar *filename;
	unsigned partition;
	unsigned size_in_bytes;
	xmlChar *start_sector;
	xmlChar *value;
	xmlChar *what;

	struct patch *next;
};

int patch_load(const char *patch_file);
int patch_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct patch *patch));
void patch_unload(void);

#endif
