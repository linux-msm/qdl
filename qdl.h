#ifndef __QDL_H__
#define __QDL_H__

#include <stdbool.h>

#include "patch.h"
#include "program.h"

struct qdl_device;

int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout);
int qdl_write(struct qdl_device *qdl, const void *buf, size_t len, bool eot);

int firehose_run(struct qdl_device *qdl, const char *incdir, const char *storage);
int sahara_run(struct qdl_device *qdl, char *prog_mbn);

extern bool qdl_debug;

#endif
