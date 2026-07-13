/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __INPUT_TYPE_H__
#define __INPUT_TYPE_H__

#include <stdbool.h>

enum {
	QDL_FILE_UNKNOWN,
	QDL_FILE_PATCH,
	QDL_FILE_PROGRAM,
	QDL_FILE_READ,
	QDL_FILE_UFS,
	QDL_FILE_CONTENTS,
	QDL_CMD_READ,
	QDL_CMD_WRITE,
	QDL_CMD_ERASE,
	QDL_CMD_FLASH,
	QDL_CMD_SHA256,
	QDL_CMD_RESET,
};

int detect_type(const char *verb);
bool qdl_is_contents_xml(const char *filename);

#endif
