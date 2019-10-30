#ifndef __UTIL_H__
#define __UTIL_H__

#include <libxml/tree.h>

void print_hex_dump(const char *prefix, const void *buf, size_t len);
unsigned int attr_as_unsigned(xmlNode *node, const char *attr, int *errors);
xmlChar *attr_as_string(xmlNode *node, const char *attr, int *errors);

#endif