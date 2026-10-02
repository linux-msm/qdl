// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "meta_cli.h"
#include "oscompat.h"
#include "pathbuf.h"
#include "qdl.h"

struct metacli_ctx {
	char *contents_xml;
	char *meta_cli_path;
	bool is_python;
	char *python_exe;
};

/**
 * metacli_find_file_recursive() - recursively search for a file
 * @dir: directory to search in
 * @target: filename to search for
 * @out: output path buffer
 * @max_depth: maximum recursion depth
 *
 * Returns: 0 if found and path is in @out, -1 if not found or error
 */
static int metacli_find_file_recursive(const char *dir, const char *target,
				       struct pathbuf *out, int max_depth)
{
	DIR *d;
	struct dirent *entry;
	struct pathbuf current_path = {};
	int ret = -1;

	if (max_depth <= 0)
		return -1;

	d = opendir(dir);
	if (!d)
		return -1;

	while ((entry = readdir(d)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		qdl_pathbuf_reset(&current_path);
		qdl_pathbuf_push(&current_path, dir);
		qdl_pathbuf_push(&current_path, entry->d_name);

		/* Check if this is the target file */
		if (!strcmp(entry->d_name, target)) {
			qdl_pathbuf_dup(out, &current_path);
			ret = 0;
			break;
		}

		/* Recurse into directories */
#ifdef _DIRENT_HAVE_D_TYPE
		if (entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN)
#endif
		{
			if (access(qdl_pathbuf_str(&current_path), X_OK) == 0) {
				ret = metacli_find_file_recursive(qdl_pathbuf_str(&current_path),
								 target, out, max_depth - 1);
				if (ret == 0)
					break;
			}
		}
	}

	closedir(d);
	return ret;
}

/**
 * metacli_find_python() - find a Python interpreter
 *
 * Tries python3, python, py in order.
 *
 * Returns: malloc'd path to python executable, or NULL if not found
 */
static char *metacli_find_python(void)
{
	const char *candidates[] = {"python3", "python", "py", NULL};
	struct qdl_process_result result;
	char *argv[] = {(char *)candidates[0], "--version", NULL};
	int i;

	for (i = 0; candidates[i]; i++) {
		argv[0] = (char *)candidates[i];
		memset(&result, 0, sizeof(result));

		if (qdl_run_capture(argv, 5000, &result) == 0) {
			qdl_process_result_free(&result);
			return strdup(candidates[i]);
		}

		qdl_process_result_free(&result);
	}

	return NULL;
}

bool metacli_locate(const char *contents_xml, struct metacli_ctx **ctx)
{
	struct metacli_ctx *c;
	struct pathbuf search_dir = {};
	struct pathbuf found_path = {};
	char *contents_dir;
	char *contents_copy;
	const char *binary_name;

	if (!contents_xml || !ctx) {
		errno = EINVAL;
		return false;
	}

	/* Check for disable flag */
	if (getenv("QDL_DISABLE_METACLI"))
		return false;

	c = calloc(1, sizeof(*c));
	if (!c) {
		errno = ENOMEM;
		return false;
	}

	c->contents_xml = strdup(contents_xml);
	if (!c->contents_xml) {
		free(c);
		errno = ENOMEM;
		return false;
	}

#ifdef _WIN32
	/* Use Windows API to normalize path */
	char normalized[MAX_PATH];
	if (GetFullPathNameA(c->contents_xml, MAX_PATH, normalized, NULL)) {
		free(c->contents_xml);
		c->contents_xml = strdup(normalized);
	}
#endif

	/* Get directory of contents.xml */
	contents_copy = strdup(contents_xml);
	if (!contents_copy) {
		free(c->contents_xml);
		free(c);
		errno = ENOMEM;
		return false;
	}

	contents_dir = dirname(contents_copy);

	/* Build search path: <contents_dir>/common/build/app */
	qdl_pathbuf_reset(&search_dir);
	qdl_pathbuf_push(&search_dir, contents_dir);
	qdl_pathbuf_push(&search_dir, "common");
	qdl_pathbuf_push(&search_dir, "build");
	qdl_pathbuf_push(&search_dir, "app");

	free(contents_copy);

	/* Determine binary name based on platform */
#ifdef _WIN32
	binary_name = "meta_cli.exe";
#else
	binary_name = "meta_cli";
#endif

	/* Try to find the binary */
	if (metacli_find_file_recursive(qdl_pathbuf_str(&search_dir), binary_name,
				       &found_path, 32) == 0) {
		c->meta_cli_path = strdup(qdl_pathbuf_str(&found_path));
		if (c->meta_cli_path) {
#ifdef _WIN32
			/* Use Windows API to normalize path */
			char normalized[MAX_PATH];
			if (GetFullPathNameA(c->meta_cli_path, MAX_PATH, normalized, NULL)) {
				free(c->meta_cli_path);
				c->meta_cli_path = strdup(normalized);
			}
#endif
		}
		c->is_python = false;
		*ctx = c;
		return true;
	}

	/* Try to find meta_cli.py */
	if (metacli_find_file_recursive(qdl_pathbuf_str(&search_dir), "meta_cli.py",
				       &found_path, 32) == 0) {
		c->meta_cli_path = strdup(qdl_pathbuf_str(&found_path));
		if (!c->meta_cli_path) {
			free(c->contents_xml);
			free(c);
			errno = ENOMEM;
			return false;
		}

#ifdef _WIN32
		/* Use Windows API to normalize path */
		char normalized[MAX_PATH];
		if (GetFullPathNameA(c->meta_cli_path, MAX_PATH, normalized, NULL)) {
			free(c->meta_cli_path);
			c->meta_cli_path = strdup(normalized);
			/* Remove duplicate backslashes */
			char *src = c->meta_cli_path, *dst = c->meta_cli_path;
			while (*src) {
				*dst++ = *src;
				if (*src == '\\' && *(src + 1) == '\\')
					src++; /* Skip duplicate backslash */
				src++;
			}
			*dst = '\0';
		}
#endif

		c->python_exe = metacli_find_python();
		if (!c->python_exe) {
			ux_err("meta_cli.py found but no Python interpreter available\n");
			free(c->meta_cli_path);
			free(c->contents_xml);
			free(c);
			errno = ENOENT;
			return false;
		}

		c->is_python = true;
		*ctx = c;
		return true;
	}

	/* Not found */
	free(c->contents_xml);
	free(c);
	return false;
}

int metacli_run_json(struct metacli_ctx *ctx, char *const cmd_argv[], struct json_value **out)
{
	struct qdl_process_result result;
	char **argv;
	char *contentsxml_arg = NULL;
	char *meta_cli_path_native = NULL;
	int argc, i;
	int ret = -1;

	if (!ctx || !cmd_argv || !out) {
		errno = EINVAL;
		return -1;
	}

	/* Count argv elements */
	for (argc = 0; cmd_argv[argc]; argc++)
		;

	if (ctx->is_python) {
		/* argv: python <meta_cli.py> --contentsxml="<path>" <cmd_argv...> */
		argv = calloc(argc + 4, sizeof(char *));
		if (!argv) {
			errno = ENOMEM;
			return -1;
		}

		argv[0] = ctx->python_exe;
		argv[1] = ctx->meta_cli_path;

		/* Build --contentsxml argument */
		size_t arg_len = strlen("--contentsxml=") + strlen(ctx->contents_xml) + 1;
		contentsxml_arg = malloc(arg_len);
		if (!contentsxml_arg) {
			free(argv);
#ifdef _WIN32
			free(meta_cli_path_native);
#endif
			errno = ENOMEM;
			return -1;
		}
		snprintf(contentsxml_arg, arg_len, "--contentsxml=%s", ctx->contents_xml);
		argv[2] = contentsxml_arg;

		for (i = 0; i < argc; i++)
			argv[3 + i] = cmd_argv[i];
		argv[3 + argc] = NULL;
		ux_debug("Running meta_cli (python): %s %s %s", argv[0], argv[1], argv[2]);
		for (i = 3; argv[i]; i++)
			ux_debug("  arg[%d]: %s", i, argv[i]);

		memset(&result, 0, sizeof(result));
		ret = qdl_run_capture(argv, 300000, &result);
		free(argv);
		free(contentsxml_arg);

	} else {
		/* argv: <meta_cli.exe> <cmd_argv...> */
		argv = calloc(argc + 2, sizeof(char *));
		if (!argv) {
			errno = ENOMEM;
			return -1;
		}

		argv[0] = ctx->meta_cli_path;

		for (i = 0; i < argc; i++)
			argv[1 + i] = cmd_argv[i];
		argv[1 + argc] = NULL;
		ux_debug("Running meta_cli (binary): %s", argv[0]);
		for (i = 1; argv[i]; i++)
			ux_debug("  arg[%d]: %s", i, argv[i]);

		memset(&result, 0, sizeof(result));
		ret = qdl_run_capture(argv, 300000, &result);
		free(argv);
	}

	if (ret < 0) {
		if (result.timed_out) {
			ux_err("meta_cli command timed out\n");
		} else {
			ux_err("meta_cli command failed with exit code %d\n", result.exit_code);
		}
		qdl_process_result_free(&result);
		return -1;
	}

	ux_debug("meta_cli stdout (%zu bytes):\n%.*s", result.stdout_len, (int)result.stdout_len, result.stdout_buf);
	/* Parse JSON output */
	*out = json_parse_buf(result.stdout_buf, result.stdout_len);
	if (!*out) {
		ux_err("failed to parse meta_cli JSON output: %s\n", json_error);
		qdl_process_result_free(&result);
		return -1;
	}

	qdl_process_result_free(&result);
	return 0;
}

void metacli_ctx_free(struct metacli_ctx *ctx)
{
	if (ctx) {
		free(ctx->contents_xml);
		free(ctx->meta_cli_path);
		free(ctx->python_exe);
		free(ctx);
	}
}
