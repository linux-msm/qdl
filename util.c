/*
 * Copyright (c) 2016, Bjorn Andersson <bjorn@kryo.se>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#ifdef _WIN32
   #include "qdl_version.h"
#else
   #include "version.h"
#endif
#include "qdl_oscompat.h"


#define MIN(x, y) ((x) < (y) ? (x) : (y))

static uint8_t to_hex(uint8_t ch)
{
	ch &= 0xf;
	return ch <= 9 ? '0' + ch : 'a' + ch - 10;
}

void print_version(char* progName)
{
	//extern const char *__progname;
	fprintf(stdout, "%s version %s\n", progName, VERSION);
}

void print_hex_dump(const char *prefix, const void *buf, size_t len)
{
	const uint8_t *ptr = buf;
	size_t linelen;
	uint8_t ch;
	char line[16 * 3 + 16 + 1];
	int li;
	int i;
	int j;

	for (i = 0; i < len; i += 16) {
		linelen = MIN(16, len - i);
		li = 0;

		for (j = 0; j < linelen; j++) {
			ch = ptr[i + j];
			line[li++] = to_hex(ch >> 4);
			line[li++] = to_hex(ch);
			line[li++] = ' ';
		}

		for (; j < 16; j++) {
			line[li++] = ' ';
			line[li++] = ' ';
			line[li++] = ' ';
		}

		for (j = 0; j < linelen; j++) {
			ch = ptr[i + j];
			line[li++] = isprint(ch) ? ch : '.';
		}

		line[li] = '\0';

		printf("%s %04x: %s\n", prefix, i, line);
	}
}

unsigned attr_as_unsigned(xmlNode *node, const char *attr, int *errors)
{
	unsigned int ret;
	xmlChar *value;

	value = xmlGetProp(node, (xmlChar*)attr);
	if (!value) {
		(*errors)++;
		return 0;
	}

	ret = (unsigned int) strtoul((char*)value, NULL, 0);
	xmlFree(value);
	return ret;
}

const char *attr_as_string(xmlNode *node, const char *attr, int *errors)
{
	xmlChar *value;
	char *ret = NULL;

	value = xmlGetProp(node, (xmlChar*)attr);
	if (!value) {
		(*errors)++;
		return NULL;
	}

	if (value[0] != '\0')
		ret = STRDUP((char*)value);

	xmlFree(value);
	return ret;
}


#ifdef _WIN32

void timeval_to_filetime(const struct timeval* tv, FILETIME* ft) {
	// Convert timeval to 100-nanosecond intervals
	LONGLONG ll = Int32x32To64(tv->tv_sec, 10000000) + tv->tv_usec * 10;
	ft->dwLowDateTime = (DWORD)ll;
	ft->dwHighDateTime = ll >> 32;
}

void filetime_to_timeval(const FILETIME* ft, struct timeval* tv) {
	// Convert 100-nanosecond intervals back to timeval
	LONGLONG ll = (((LONGLONG)ft->dwHighDateTime) << 32) + ft->dwLowDateTime;
	tv->tv_sec = (long)(ll / 10000000);
	tv->tv_usec = (long)((ll % 10000000) / 10);
}

void timeradd(const struct timeval* a, const struct timeval* b, struct timeval* res) {
	FILETIME ftA, ftB, ftRes;

	// Convert timeval to FILETIME
	timeval_to_filetime(a, &ftA);
	timeval_to_filetime(b, &ftB);

	// Add FILETIME values
	ULARGE_INTEGER ulA, ulB, ulRes;
	ulA.LowPart = ftA.dwLowDateTime;
	ulA.HighPart = ftA.dwHighDateTime;

	ulB.LowPart = ftB.dwLowDateTime;
	ulB.HighPart = ftB.dwHighDateTime;

	ulRes.QuadPart = ulA.QuadPart + ulB.QuadPart;

	ftRes.dwLowDateTime = ulRes.LowPart;
	ftRes.dwHighDateTime = ulRes.HighPart;

	// Convert back to timeval
	filetime_to_timeval(&ftRes, res);
}

HANDLE openatFd;
int OPENAT(const char* ramdump_path, const char* fileName, int ramdump_dir)
{

	openatFd = CreateFile(("%s\\%s", ramdump_path, fileName), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (openatFd == INVALID_HANDLE_VALUE) {

		return -1;
	}
	return 0;
}

void CLOSEHANDLE(int fd)
{
	CloseHandle(openatFd);
}

int PATHMATCH(const char* pattern, const char* filename)
{
	BOOL match;
	int retValue = 1;
	match = PathMatchSpecW(filename, pattern);
	if (match)
	{
		retValue = 0;  //based on originla Linux fnmatch, expect 0 when matched. 
	}
	return retValue;
}

HANDLE ramdump_dirHandler = -1;
int OPENDIRECTORY(const char* ramdump_path) {
	int ramdump_dir = 0;

	WIN32_FIND_DATA findFileData;
	ramdump_dirHandler = FindFirstFile(("%s\\*", ramdump_path), &findFileData);
	if (ramdump_dirHandler == INVALID_HANDLE_VALUE) {
		ramdump_dir = -1;
	}
	return        ramdump_dir;
}

void CLOSERAMDUMPHANDLE(int ramdump_dir)
{
	CloseHandle(ramdump_dirHandler);
}

/*
void err(int status, const char* format)
{
	fprintf(stderr, "%s: select: %d\n", format, status);

}*/

void errx(int exit_code, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(exit_code);
}

char** ListFiles(const char* pattern, int* count) {
	WIN32_FIND_DATA findFileData;
	HANDLE hFind = FindFirstFile(pattern, &findFileData);
	char** fileList = NULL;
	*count = 0;

	if (hFind == INVALID_HANDLE_VALUE) {
		printf("No files found.\n");
		return NULL;
	}

	do {
		fileList = realloc(fileList, (*count + 1) * sizeof(char*));
		fileList[*count] = _strdup(findFileData.cFileName);
		(*count)++;
	} while (FindNextFile(hFind, &findFileData) != 0);

	FindClose(hFind);
	return fileList;
}

#else
//Linux and Mac implementation
int OPENAT(const char* ramdump_path, const char* fileName, int ramdump_dir)
{
	int fd;
	fd = openat(ramdump_dir, fileName, O_WRONLY | O_CREAT, 0644);
	return fd;
}

void CLOSEHANDLE(int fd)
{
	close(fd);
}

int PATHMATCH(const char* pattern, const char* filename)
{
	int match = fnmatch(pattern, filename, 0);
	return match;
}

int OPENDIRECTORY(const char* ramdump_path) {
	int ramdump_dir = -1;
	ramdump_dir = open(ramdump_path, O_DIRECTORY);
	return        ramdump_dir;
}

void CLOSERAMDUMPHANDLE(int ramdump_dir)
{
	close(ramdump_dir);
}
#endif
