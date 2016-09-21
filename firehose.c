#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "qdl.h"

static void xml_setpropf(xmlNode *node, const char *attr, const char *fmt, ...)
{
	xmlChar buf[128];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf((char*)buf, sizeof(buf), fmt, ap);
	xmlSetProp(node, (xmlChar*)attr, buf);
	va_end(ap);
}

static xmlNode *firehose_response_parse(const void *buf, size_t len, int *error)
{
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;

	doc = xmlReadMemory(buf, len, NULL, NULL, 0);
	if (!doc) {
		fprintf(stderr, "failed to parse firehose packet\n");
		*error = -EINVAL;
		return NULL;
	}

	root = xmlDocGetRootElement(doc);
	for (node = root; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrcmp(node->name, (xmlChar*)"data") == 0)
			break;
	}

	if (!node) {
		fprintf(stderr, "firehose packet without data tag\n");
		*error = -EINVAL;
		xmlFreeDoc(doc);
		return NULL;
	}

	for (node = node->children; node && node->type != XML_ELEMENT_NODE; node = node->next)
		;

	return node;
}

static void firehose_response_log(xmlNode *node)
{
	xmlChar *value;

	value = xmlGetProp(node, (xmlChar*)"value");
	printf("LOG: %s\n", value);
}

static int firehose_wait(int fd, int timeout)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = fd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, timeout);
	if (ret == 0)
		return -ETIMEDOUT;

	return ret < 0 ? ret : 0;;
}

static int firehose_read(int fd, int timeout, int (*response_parser)(xmlNode *node))
{
	char buf[4096];
	xmlNode *nodes;
	xmlNode *node;
	int error;
	int ret = -ENXIO;
	int n;

	for (;;) {
		ret = firehose_wait(fd, timeout);
		if (ret < 0)
			return ret;

		n = read(fd, buf, sizeof(buf));
		if (n == 0) {
			continue;
		} else if (n < 0) {
			warn("failed to read");
			break;
		}
		buf[n] = '\0';

		// printf("%s\n", buf);
		if (qdl_debug)
			fprintf(stderr, "FIREHOSE READ: %s\n", buf);

		nodes = firehose_response_parse(buf, n, &error);
		if (!nodes) {
			fprintf(stderr, "unable to parse response\n");
			return error;
		}

		ret = -EAGAIN;
		for (node = nodes; node; node = node->next) {
			if (xmlStrcmp(node->name, (xmlChar*)"log") == 0)
				firehose_response_log(node);
			else if (response_parser && xmlStrcmp(node->name, (xmlChar*)"response") == 0)
				ret = response_parser(node);
		}

		xmlFreeDoc(nodes->doc);

		if (ret == -EAGAIN)
			continue;
		break;
	}

	return ret;
}

static int firehose_write(int fd, xmlDoc *doc)
{
	int saved_errno;
	xmlChar *s;
	int len;
	int ret;

	xmlDocDumpMemory(doc, &s, &len);

	if (qdl_debug)
		fprintf(stderr, "FIREHOSE WRITE: %s\n", s);

	ret = write(fd, s, len);
	saved_errno = errno;
	xmlFree(s);
	return ret < 0 ? -saved_errno : 0;
}

static int firehose_nop_parser(xmlNode *node)
{
	xmlChar *value;

	value = xmlGetProp(node, (xmlChar*)"value");
	return !!xmlStrcmp(value, (xmlChar*)"ACK");
}

static int firehose_nop(int fd)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"nop", NULL);
	xml_setpropf(node, "value", "ping");

	ret = firehose_write(fd, doc);
	if (ret < 0)
		return ret;

	return firehose_read(fd, -1, firehose_nop_parser);
}

static int firehose_getstorageinfo_parser(xmlNode *node)
{
	xmlChar *value;

	value = xmlGetProp(node, (xmlChar*)"value");
	if (value)
		return -EINVAL;

#if 0
	xmlGetProp("num_partition_sectors");
	xmlGetProp("SECTOR_SIZE_IN_BYTES");
	xmlGetProp("num_physical_partitions");
	printf("%s\n", node->name);
#endif
	return 0;
}

static int firehose_getstorageinfo(int fd, int partition)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"getstorageinfo", NULL);
	xml_setpropf(node, "physical_partition_number", "%d", partition);

	ret = firehose_write(fd, doc);
	if (ret < 0)
		return ret;

	return firehose_read(fd, -1, firehose_getstorageinfo_parser);
}

static int firehose_configure_parser(xmlNode *node)
{
	return 0;
}

static int firehose_configure(int fd)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"configure", NULL);
	xml_setpropf(node, "MemoryName", "ufs");
	xml_setpropf(node, "MaxPayloadSizeToTargetInBytes", "%d", 8192);
	xml_setpropf(node, "verbose", "%d", 0);
	xml_setpropf(node, "ZLPAwareHost", "%d", 0);

	ret = firehose_write(fd, doc);
	if (ret < 0)
		return ret;

	return firehose_read(fd, -1, firehose_nop_parser);
}

static int firehose_program(int usbfd, struct program *program, int fd)
{
	unsigned num_sectors;
	struct stat sb;
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	void *buf;
	int left;
	int ret;
	int n;

	num_sectors = program->num_sectors;

	if (fd >= 0 && num_sectors == 0) {
		fstat(fd, &sb);
		num_sectors = (sb.st_size + program->sector_size - 1) / program->sector_size;
	}

	buf = malloc(program->sector_size);
	if (!buf)
		err(1, "failed to allocate sector buffer");

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"program", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", program->sector_size);
	xml_setpropf(node, "num_partition_sectors", "%d", num_sectors);
	xml_setpropf(node, "physical_partition_number", "%d", program->partition);
	xml_setpropf(node, "start_sector", "%s", program->start_sector);
	if (program->filename)
		xml_setpropf(node, "filename", "%s", program->filename);

	ret = firehose_write(usbfd, doc);
	if (ret < 0) {
		fprintf(stderr, "[PROGRAM] failed to write program command\n");
		goto out;
	}

	ret = firehose_read(usbfd, -1, firehose_nop_parser);
	if (ret) {
		fprintf(stderr, "[PROGRAM] failed to setup programming\n");
		goto out;
	}

	lseek(fd, program->file_offset * program->sector_size, SEEK_SET);
	for (left = num_sectors; left > 0; left--) {
		if (fd >= 0) {
			n = read(fd, buf, program->sector_size);
			if (n < 0)
				err(1, "failed to read");
		} else {
			n = 0;
		}

		if (n < program->sector_size)
			memset(buf + n, 0, program->sector_size - n);

		n = write(usbfd, buf, program->sector_size);
		if (n < 0)
			err(1, "failed to write");

		if (n != program->sector_size)
			err(1, "failed to write full sector");
	}

	ret = firehose_read(usbfd, -1, firehose_nop_parser);
	if (ret)
		fprintf(stderr, "[PROGRAM] failed\n");

out:
	xmlFreeDoc(doc);
	return ret;
}

static int firehose_apply_patch(int fd, struct patch *patch)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	printf("%s\n", patch->what);

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"patch", NULL);
	xml_setpropf(node, "SECTOR_SIZE_IN_BYTES", "%d", patch->sector_size);
	xml_setpropf(node, "byte_offset", "%d", patch->byte_offset);
	xml_setpropf(node, "filename", "%s", patch->filename);
	xml_setpropf(node, "physical_partition_number", "%d", patch->partition);
	xml_setpropf(node, "size_in_bytes", "%d", patch->size_in_bytes);
	xml_setpropf(node, "start_sector", "%s", patch->start_sector);
	xml_setpropf(node, "value", "%s", patch->value);

	ret = firehose_write(fd, doc);
	if (ret < 0)
		goto out;

	ret = firehose_read(fd, -1, firehose_nop_parser);
	if (ret)
		fprintf(stderr, "[APPLY PATCH] %d\n", ret);

out:
	xmlFreeDoc(doc);
	return ret;
}

static int firehose_set_bootable(int fd, int lun)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"setbootablestoragedrive", NULL);
	xml_setpropf(node, "value", "%d", lun);

	ret = firehose_write(fd, doc);
	if (ret < 0)
		return ret;

	ret = firehose_read(fd, -1, firehose_nop_parser);
	if (ret) {
		fprintf(stderr, "failed to set LUN%d as bootable device\n", lun);
		return -1;
	}

	printf("LUN%d is now bootable device\n", lun);
	return 0;
}

static int firehose_reset(int fd)
{
	xmlNode *root;
	xmlNode *node;
	xmlDoc *doc;
	int ret;

	doc = xmlNewDoc((xmlChar*)"1.0");
	root = xmlNewNode(NULL, (xmlChar*)"data");
	xmlDocSetRootElement(doc, root);

	node = xmlNewChild(root, NULL, (xmlChar*)"power", NULL);
	xml_setpropf(node, "value", "reset");

	ret = firehose_write(fd, doc);
	if (ret < 0)
		return ret;

	return firehose_read(fd, -1, firehose_nop_parser);
}

int firehose_run(int fd)
{
	int ret;

	ret = firehose_wait(fd, 10000);
	if (ret < 0 && ret != -ETIMEDOUT)
		return ret;

	while (firehose_read(fd, 100, NULL) != -ETIMEDOUT)
		;

	ret = firehose_nop(fd);
	if (ret)
		return ret;

	ret = firehose_configure(fd);
	if (ret)
		return ret;

#if 0
	int i;
	for (i = 0; i < 7; i++) {
		printf("- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -\n");
		printf("LUN%d\n", i);

		ret = firehose_getstorageinfo(fd, i);
		printf("getstorageinfo: %d\n", ret);
	}
#endif

	ret = program_execute(fd, firehose_program);
	if (ret)
		return ret;

	ret = patch_execute(fd, firehose_apply_patch);
	if (ret)
		return ret;

	firehose_set_bootable(fd, 1);

	firehose_reset(fd);

	return 0;
}
