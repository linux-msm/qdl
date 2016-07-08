#include <errno.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "patch.h"
		
static struct patch *patches;
static struct patch *patches_last;

static unsigned attr_as_unsigned(xmlNode *node, const char *attr, int *errors)
{
	xmlChar *value;	

	value = xmlGetProp(node, (xmlChar*)attr);
	if (!value)
		(*errors)++;

	return strtoul((char*)value, NULL, 10);
}

static const char *attr_as_string(xmlNode *node, const char *attr, int *errors)
{
	xmlChar *value;	

	value = xmlGetProp(node, (xmlChar*)attr);
	if (!value)
		(*errors)++;

	return strdup((char*)value);
}

int patch_load(const char *patch_file)
{
	struct patch *patch;
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;
	int errors;

	doc = xmlReadFile(patch_file, NULL, 0);
	if (!doc) {
		fprintf(stderr, "[PATCH] failed to parse %s\n", patch_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	for (node = root->children; node ; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name, (xmlChar*)"patch")) {
			fprintf(stderr, "[PATCH] unrecognized tag \"%s\", ignoring\n", node->name);
			continue;
		}

		errors = 0;

		patch = calloc(1, sizeof(struct patch));

		patch->sector_size = attr_as_unsigned(node, "SECTOR_SIZE_IN_BYTES", &errors);
		patch->byte_offset = attr_as_unsigned(node, "byte_offset", &errors);
		patch->filename = attr_as_string(node, "filename", &errors);
		patch->partition = attr_as_unsigned(node, "physical_partition_number", &errors);
		patch->size_in_bytes = attr_as_unsigned(node, "size_in_bytes", &errors);
		patch->start_sector = attr_as_string(node, "start_sector", &errors);
		patch->value = attr_as_string(node, "value", &errors);
		patch->what = attr_as_string(node, "what", &errors);

		if (errors) {
			fprintf(stderr, "[PATCH] errors while parsing patch\n");
			free(patch);
			continue;
		}

		if (patches) {
			patches_last->next = patch;
			patches_last = patch;
		} else {
			patches = patch;
			patches_last = patch;
		}
	}

	xmlFreeDoc(doc);

	return 0;
}
	
int patch_execute(int fd, void (*apply)(int fd, struct patch *patch))
{
	struct patch *patch;

	for (patch = patches; patch; patch = patch->next) {
		if (strcmp(patch->filename, "DISK"))
			continue;

		apply(fd, patch);
	}

	return 0;
}
