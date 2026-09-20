/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __CLI_H__
#define __CLI_H__

#include <stdbool.h>
#include <stdio.h>

/* Long-only option ids, distinct from any short option character. */
enum {
	OPT_BACKEND = 0x100,
	OPT_SKIPBLOCK,
	OPT_SERIAL,
};

/* Command-line options and flags for a flashing run. */
struct qdl_opts {
	enum qdl_storage_type storage_type;
	enum QDL_DEVICE_TYPE dev_type;
	enum qdl_skipblock_mode skipblock_mode;
	char *incdir;
	char *serial;
	const char *vip_generate_dir;
	const char *vip_table_path;
	long out_chunk_size;
	unsigned int slot;
	bool finalize_provisioning;
	bool allow_fusing;
	bool allow_missing;
	bool skip_reset;
};

/* Result of qdl_parse_args(). */
enum {
	QDL_ARGS_OK,	/* options parsed, positional arguments follow */
	QDL_ARGS_DONE,	/* handled entirely (--help/--version); exit 0 */
	QDL_ARGS_ERROR,	/* usage error; exit 1 */
};

int qdl_parse_args(int argc, char **argv, struct qdl_opts *opts);
void print_usage(FILE *out);

#endif
