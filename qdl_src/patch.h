#ifndef __PATCH_H__
#define __PATCH_H__

struct qdl_device;

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
int patch_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct patch *patch, unsigned int read_timeout, unsigned int write_timeout), unsigned int read_timeout, unsigned int write_timeout);

#endif
