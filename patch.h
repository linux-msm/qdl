#ifndef PATCH_H
#define PATCH_H

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

int patch_execute(struct qdl_device *ctx, int (*apply)(struct qdl_device *ctx, struct patch *patch));

#endif /* PATCH_H */
