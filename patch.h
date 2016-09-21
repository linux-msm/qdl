#ifndef __PATCH_H__
#define __PATCH_H__

struct patch {
	unsigned sector_size;
	unsigned byte_offset;
	const char *filename;
	unsigned partition;
	unsigned size_in_bytes;
	const char *start_sector;
	const char *value;
	const char *what;

	struct patch *next;
};

int patch_load(const char *patch_file);
int patch_execute(int fd, int (*apply)(int fd, struct patch *patch));

#endif
