#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "program.h"
		
static struct program *programes;
static struct program *programes_last;

static unsigned attr_as_unsigned(xmlNode *node, const char *attr, int *errors)
{
	xmlChar *value;	

	value = xmlGetProp(node, (xmlChar*)attr);
	if (!value)
		(*errors)++;

	return strtoul((char*)value, NULL, 10);
}

static unsigned attr_as_unsignedhex(xmlNode *node, const char *attr, int *errors)
{
	xmlChar *value;	

	value = xmlGetProp(node, (xmlChar*)attr);
	if (!value)
		(*errors)++;

	return strtoul((char*)value, NULL, 16);
}

static const char *attr_as_string(xmlNode *node, const char *attr, int *errors)
{
	xmlChar *value;	

	value = xmlGetProp(node, (xmlChar*)attr);
	if (!value)
		(*errors)++;

	if (value && value[0] == '\0')
		return NULL;

	return strdup((char*)value);
}

static bool attr_as_bool(xmlNode *node, const char *attr, int *errors)
{
	xmlChar *value;	

	value = xmlGetProp(node, (xmlChar*)attr);
	if (!value)
		(*errors)++;

	return xmlStrcmp(value, (xmlChar*)"true") == 0;
}

int program_load(const char *program_file)
{
	struct program *program;
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;
	int errors;

	doc = xmlReadFile(program_file, NULL, 0);
	if (!doc) {
		fprintf(stderr, "[PROGRAM] failed to parse %s\n", program_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	for (node = root->children; node ; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name, (xmlChar*)"program")) {
			fprintf(stderr, "[PROGRAM] unrecognized tag \"%s\", ignoring\n", node->name);
			continue;
		}

		errors = 0;

		program = calloc(1, sizeof(struct program));

		program->sector_size = attr_as_unsigned(node, "SECTOR_SIZE_IN_BYTES", &errors);
		program->file_offset = attr_as_unsigned(node, "file_sector_offset", &errors);
		program->filename = attr_as_string(node, "filename", &errors);
		program->label = attr_as_string(node, "label", &errors);
		program->num_sectors = attr_as_unsigned(node, "num_partition_sectors", &errors);
		program->partition = attr_as_unsigned(node, "physical_partition_number", &errors);
		program->readback = attr_as_bool(node, "readbackverify", &errors);
		program->size = attr_as_unsigned(node, "size_in_KB", &errors);
		program->sparse = attr_as_bool(node, "sparse", &errors);
		program->start_bytes = attr_as_string(node, "start_byte_hex", &errors);
		program->start_sector = attr_as_string(node, "start_sector", &errors);

		if (errors) {
			fprintf(stderr, "[PROGRAM] errors while parsing program\n");
			free(program);
			continue;
		}

		if (programes) {
			programes_last->next = program;
			programes_last = program;
		} else {
			programes = program;
			programes_last = program;
		}
	}

	xmlFreeDoc(doc);

	return 0;
}
	
int program_execute(int usbfd, void (*apply)(int usbfd, struct program *program, int fd))
{
	struct program *program;
	int fd;

	for (program = programes; program; program = program->next) {
		fd = -1;
		if (program->filename) {
			fd = open(program->filename, O_RDONLY);
			if (fd < 0) {
				printf("Unable to open %s...ignoring\n", program->filename);
				continue;
			}
		}
		apply(usbfd, program, fd);

		close(fd);
	}

	return 0;
}

