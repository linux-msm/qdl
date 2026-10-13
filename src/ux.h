/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __UX_H__
#define __UX_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void ux_init(void);
void ux_set_progname(const char *name);
const char *ux_progname(void);
void ux_err(const char *fmt, ...);
void ux_warn(const char *fmt, ...);
void ux_warnx(const char *fmt, ...);
void ux_die(int status, const char *fmt, ...) __attribute__((noreturn));
void ux_diex(int status, const char *fmt, ...) __attribute__((noreturn));
void ux_info(const char *fmt, ...);
void ux_log(const char *fmt, ...);
void ux_debug(const char *fmt, ...);
void ux_progress(const char *fmt, unsigned int value, unsigned int size, ...);

/* When to colorize output, selected with --color[=<auto|always|never>] */
enum ux_color_mode {
	UX_COLOR_AUTO,
	UX_COLOR_ALWAYS,
	UX_COLOR_NEVER,
};

/* Text styles, emitted as ANSI escape sequences when color is enabled */
enum ux_style {
	UX_STYLE_RESET,
	UX_STYLE_HEADING,
	UX_STYLE_OPTION,
	UX_STYLE_ERROR,
	UX_STYLE_WARNING,
	UX_STYLE_LOG,
	UX_STYLE_DIM,
	UX_STYLE_PROGRESS,
};

void ux_set_color_mode(enum ux_color_mode mode);
const char *ux_style(FILE *fp, enum ux_style style);

int decode_color_mode(const char *name, enum ux_color_mode *out);

#endif
