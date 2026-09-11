// SPDX-License-Identifier: BSD-3-Clause
/*
 * Classification of the positional inputs of the flashing flow: command
 * verbs, and XML input files told apart by their root and child
 * elements.
 */
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "qdl.h"
#include "input_type.h"

int detect_type(const char *verb)
{
	xmlNode *root;
	xmlDoc *doc;
	xmlNode *node;
	int type = QDL_FILE_UNKNOWN;

	if (!strcmp(verb, "read"))
		return QDL_CMD_READ;
	if (!strcmp(verb, "write"))
		return QDL_CMD_WRITE;
	if (!strcmp(verb, "erase"))
		return QDL_CMD_ERASE;
	if (!strcmp(verb, "flash"))
		return QDL_CMD_FLASH;
	if (!strcmp(verb, "sha256"))
		return QDL_CMD_SHA256;
	if (!strcmp(verb, "reset"))
		return QDL_CMD_RESET;

	if (access(verb, F_OK)) {
		ux_err("%s is not a verb and not a XML file\n", verb);
		return -EINVAL;
	}

	doc = xmlReadFile(verb, NULL, 0);
	if (!doc) {
		ux_err("failed to parse XML file \"%s\"\n", verb);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	if (!xmlStrcmp(root->name, (xmlChar *)"patches")) {
		type = QDL_FILE_PATCH;
	} else if (!xmlStrcmp(root->name, (xmlChar *)"data")) {
		for (node = root->children; node ; node = node->next) {
			if (node->type != XML_ELEMENT_NODE)
				continue;
			if (!xmlStrcmp(node->name, (xmlChar *)"program")) {
				type = QDL_FILE_PROGRAM;
				break;
			}
			if (!xmlStrcmp(node->name, (xmlChar *)"read")) {
				type = QDL_FILE_READ;
				break;
			}
			if (!xmlStrcmp(node->name, (xmlChar *)"ufs")) {
				type = QDL_FILE_UFS;
				break;
			}
		}
	} else if (!xmlStrcmp(root->name, (xmlChar *)"contents")) {
		type = QDL_FILE_CONTENTS;
	}

	xmlFreeDoc(doc);

	return type;
}

bool qdl_is_contents_xml(const char *filename)
{
	xmlNode *root;
	xmlDoc *doc;
	bool ret;

	doc = xmlReadFile(filename, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (!doc)
		return false;

	root = xmlDocGetRootElement(doc);
	ret = root && !xmlStrcmp(root->name, (xmlChar *)"contents");

	xmlFreeDoc(doc);

	return ret;
}

