// SPDX-License-Identifier: BSD-3-Clause
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"

int test_make_temp_dir(char *buf, size_t size, const char *prefix)
{
	int ret;

#ifdef _WIN32
	const char *tmp_base = getenv("TMPDIR");

	if (!tmp_base || tmp_base[0] == '\0')
		tmp_base = getenv("TEMP");
	if (!tmp_base || tmp_base[0] == '\0')
		tmp_base = getenv("TMP");
	if (!tmp_base || tmp_base[0] == '\0')
		tmp_base = ".";

	ret = snprintf(buf, size, "%s/%s-XXXXXX", tmp_base, prefix);
#else
	ret = snprintf(buf, size, "/tmp/%s-XXXXXX", prefix);
#endif

	if (ret <= 0 || (size_t)ret >= size)
		return -1;

	return mkdtemp(buf) ? 0 : -1;
}
