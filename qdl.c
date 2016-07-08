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
#include "patch.h"

enum {
	QDL_FILE_UNKNOWN,
	QDL_FILE_PATCH,
	QDL_FILE_PROGRAM,
	QDL_FILE_CONTENTS,
};

static int detect_type(const char *xml_file)
{
	xmlNode *root;
	xmlDoc *doc;
	int type;

	doc = xmlReadFile(xml_file, NULL, 0);
	if (!doc) {
		fprintf(stderr, "[PATCH] failed to parse %s\n", xml_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	if (!xmlStrcmp(root->name, (xmlChar*)"patches"))
		type = QDL_FILE_PATCH;
	else if (!xmlStrcmp(root->name, (xmlChar*)"data"))
		type = QDL_FILE_PROGRAM;
	else if (!xmlStrcmp(root->name, (xmlChar*)"contents"))
		type = QDL_FILE_CONTENTS;
	else
		type = QDL_FILE_UNKNOWN;

	xmlFreeDoc(doc);

	return type;
}

static int readat(int dir, const char *name, char *buf, size_t len)
{
	ssize_t n;
	int fd;
	int ret = 0;

	fd = openat(dir, name, O_RDONLY);
	if (fd < 0)
		return fd;

	n = read(fd, buf, len - 1);
	if (n < 0) {
		warn("failed to read %s", name);
		ret = -EINVAL;
		goto close_fd;
	}
	buf[n] = '\0';

	buf[strcspn(buf, "\n")] = '\0';

close_fd:
	close(fd);
	return ret;
}

static int find_qdl_tty(char *dev_name, size_t dev_name_len)
{
	struct dirent *de;
	int found = -ENOENT;
	char vid[5];
	char pid[5];
	DIR *dir;
	int tty;
	int fd;
	int ret;

	tty = open("/sys/class/tty", O_DIRECTORY);
	if (tty < 0)
		err(1, "failed to open /sys/class/tty");

	dir = fdopendir(tty);
	if (!dir)
		err(1, "failed to opendir /sys/class/tty");

	while ((de = readdir(dir)) != NULL) {
		if (strncmp(de->d_name, "ttyUSB", 6) != 0)
			continue;

		fd = openat(tty, de->d_name, O_DIRECTORY);
		if (fd < 0)
			continue;

		ret = readat(fd, "../../../../idVendor", vid, sizeof(vid));
		if (ret < 0)
			goto close_fd;

		ret = readat(fd, "../../../../idProduct", pid, sizeof(pid));
		if (ret < 0)
			goto close_fd;

		if (strcmp(vid, "05c6") || strcmp(pid, "9008"))
			goto close_fd;

		snprintf(dev_name, dev_name_len, "/dev/%s", de->d_name);
		found = 0;

close_fd:
		close(fd);
	}

	closedir(dir);
	close(tty);

	return found;

}

static int tty_open(struct termios *old)
{
	struct termios tios;
	char buf[80];
	int ret;
	int fd;

retry:
	ret = find_qdl_tty(buf, sizeof(buf));
	if (ret < 0) {
		printf("Waiting for QDL tty...\r");
		fflush(stdout);
		sleep(1);
		goto retry;
	}

	fd = open(buf, O_RDWR | O_NOCTTY | O_EXCL);
	if (fd < 0) {
		err(1, "unable to open \"%s\"", buf);
	}

	ret = tcgetattr(fd, old);
	if (ret < 0)
		err(1, "unable to retrieve \"%s\" tios", buf);

	memset(&tios, 0, sizeof(tios));
	tios.c_cflag = B115200 | CRTSCTS | CS8 | CLOCAL | CREAD;
	tios.c_iflag = IGNPAR;
	tios.c_oflag = 0;

	tcflush(fd, TCIFLUSH);

	ret = tcsetattr(fd, TCSANOW, &tios);
	if (ret < 0)
		err(1, "unable to update \"%s\" tios", buf);

	return fd;
}

int main(int argc, char **argv)
{
	extern const char *__progname;
	struct termios tios;
	char *prog_mbn;
	int type;
	int ret;
	int fd;
	int i;

	if (argc < 3) {
		fprintf(stderr, "%s <prog.mbn> [<program> <patch> ...]\n", __progname);
		return 1;
	}

	prog_mbn = argv[1];

	for (i = 2; i < argc; i++) {
		type = detect_type(argv[i]);
		if (type < 0 || type == QDL_FILE_UNKNOWN)
			errx(1, "failed to detect file type of %s\n", argv[i]);

		switch (type) {
		case QDL_FILE_PATCH:
			ret = patch_load(argv[i]);
			if (ret < 0)
				errx(1, "patch_load %s failed", argv[i]);
			break;
		case QDL_FILE_PROGRAM:
			ret = program_load(argv[i]);
			if (ret < 0)
				errx(1, "program_load %s failed", argv[i]);
			break;
		default:
			errx(1, "%s type not yet supported", argv[i]);
			break;
		}
	}

	fd = tty_open(&tios);
	if (fd < 0)
		err(1, "failed to open QDL tty");

	ret = sahara_run(fd, prog_mbn);
	if (ret < 0)
		goto out;

	ret = firehose_run(fd);

out:
	ret = tcsetattr(fd, TCSANOW, &tios);
	if (ret < 0)
		warn("unable to restore tios of ttyUSB1");
	close(fd);

	return 0;
}
