#ifndef __QDL_H__
#define __QDL_H__

#include <stdbool.h>

#include "patch.h"
#include "program.h"

int firehose_run(int fd);
int sahara_run(int fd, char *prog_mbn);
void print_hex_dump(const char *prefix, const void *buf, size_t len);

int content_load(const char *content_file);

extern bool qdl_debug;

#endif
