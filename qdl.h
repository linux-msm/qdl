#ifndef __QDL_H__
#define __QDL_H__

#include <stdbool.h>

#include "patch.h"
#include "program.h"
#include <libxml/tree.h>

#define MAPPING_SZ 64

struct qdl_device {
        int fd;

        int in_ep;
        int out_ep;

        size_t in_maxpktsize;
        size_t out_maxpktsize;

        char *mappings[MAPPING_SZ]; // array index is the id from the device
};

int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout);
int qdl_write(struct qdl_device *qdl, const void *buf, size_t len);

int firehose_run(struct qdl_device *qdl, const char *incdir, const char *storage);
int sahara_run(struct qdl_device *qdl, char *img_arr[], bool single_image);
void print_hex_dump(const char *prefix, const void *buf, size_t len);
unsigned attr_as_unsigned(xmlNode *node, const char *attr, int *errors);
const char *attr_as_string(xmlNode *node, const char *attr, int *errors);

extern bool qdl_debug;

#endif
