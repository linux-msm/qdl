/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __META_CLI_H__
#define __META_CLI_H__

#include <stdbool.h>
#include "json.h"

struct metacli_ctx;

/**
 * metacli_locate() - locate meta_cli binary near contents.xml
 * @contents_xml: path to contents.xml file
 * @ctx: output context to be filled
 *
 * Searches recursively under <dirname(contents_xml)>/common/build/app for
 * meta_cli.exe (Windows) or meta_cli (Linux), falling back to meta_cli.py.
 *
 * Returns: true if meta_cli was found and context is populated, false if not found.
 * On error, returns false and sets errno.
 */
bool metacli_locate(const char *contents_xml, struct metacli_ctx **ctx);

/**
 * metacli_run_json() - run a meta_cli command and parse JSON output
 * @ctx: meta_cli context from metacli_locate()
 * @cmd_argv: NULL-terminated argv for the command (e.g., {"get_storage_types", NULL})
 * @out: output JSON value (caller must free with json_free())
 *
 * Runs the meta_cli command with a 300 second timeout. The command output
 * is parsed as JSON and returned in @out.
 *
 * Returns: 0 on success, -1 on error (with errno set or ux_err() called).
 */
int metacli_run_json(struct metacli_ctx *ctx, char *const cmd_argv[], struct json_value **out);

/**
 * metacli_ctx_free() - free meta_cli context
 * @ctx: context to free
 */
void metacli_ctx_free(struct metacli_ctx *ctx);

#endif
