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

#endif
