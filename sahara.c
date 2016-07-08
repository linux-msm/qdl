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
#include "qdl.h"

struct sahara_pkt {
	uint32_t cmd;
	uint32_t length;

	union {
		struct {
			uint32_t version;
			uint32_t compatible;
			uint32_t max_len;
			uint32_t mode;
		} hello_req;
		struct {
			uint32_t version;
			uint32_t compatible;
			uint32_t status;
			uint32_t mode;
		} hello_resp;
		struct {
			uint32_t image;
			uint32_t offset;
			uint32_t length;
		} read_req;
		struct {
			uint32_t image;
			uint32_t status;
		} eoi;
		struct {
		} done_req;
		struct {
			uint32_t status;
		} done_resp;
		struct {
			uint32_t image;
			uint64_t offset;
			uint64_t length;
		} read64_req;
	};
};

static void sahara_hello(int fd, struct sahara_pkt *pkt)
{
	struct sahara_pkt resp;

	assert(pkt->length == 0x30);

	printf("HELLO version: 0x%x compatible: 0x%x max_len: %d mode: %d\n",
	       pkt->hello_req.version, pkt->hello_req.compatible, pkt->hello_req.max_len, pkt->hello_req.mode);

	resp.cmd = 2;
	resp.length = 0x30;
	resp.hello_resp.version = 2;
	resp.hello_resp.compatible = 1;
	resp.hello_resp.status = 0;
	resp.hello_resp.mode = pkt->hello_req.mode;

	write(fd, &resp, resp.length);
}

static int sahara_read_common(int fd, const char *mbn, off_t offset, size_t len)
{
	int progfd;
	ssize_t n;
	void *buf;
	int ret = 0;

	progfd = open(mbn, O_RDONLY);
	if (progfd < 0)
		return -errno;

	buf = malloc(len);
	if (!buf)
		return -ENOMEM;

	lseek(progfd, offset, SEEK_SET);
	n = read(progfd, buf, len);
	if (n != len) {
		ret = -errno;
		goto out;
	}

	n = write(fd, buf, n);
	if (n != len)
		err(1, "failed to write %zu bytes to sahara", len);

	free(buf);
	close(progfd);

out:
	return ret;
}

static void sahara_read(int fd, struct sahara_pkt *pkt, const char *mbn)
{
	int ret;

	assert(pkt->length = 0x14);

	printf("READ image: %d offset: 0x%x length: 0x%x\n",
	       pkt->read_req.image, pkt->read_req.offset, pkt->read_req.length);

	ret = sahara_read_common(fd, mbn, pkt->read_req.offset, pkt->read_req.length);
	if (ret < 0)
		errx(1, "failed to read image chunk to sahara");
}

static void sahara_read64(int fd, struct sahara_pkt *pkt, const char *mbn)
{
	int ret;

	assert(pkt->length = 0x20);

	printf("READ64 image: %d offset: 0x%lx length: 0x%lx\n",
	       pkt->read64_req.image, pkt->read64_req.offset, pkt->read64_req.length);

	ret = sahara_read_common(fd, mbn, pkt->read64_req.offset, pkt->read64_req.length);
	if (ret < 0)
		errx(1, "failed to read image chunk to sahara");
}

static void sahara_eoi(int fd, struct sahara_pkt *pkt)
{
	struct sahara_pkt done;

	assert(pkt->length == 0x10);

	printf("END OF IMAGE image: %d status: %d\n", pkt->eoi.image, pkt->eoi.status);

	if (pkt->eoi.status != 0) {
		printf("received non-successful result\n");
		return;
	}

	done.cmd = 5;
	done.length = 0x8;
	write(fd, &done, done.length);
}

static int sahara_done(int fd, struct sahara_pkt *pkt)
{
	assert(pkt->length == 0xc);

	printf("DONE status: %d\n", pkt->done_resp.status);

	return pkt->done_resp.status;
}

int sahara_run(int fd, char *prog_mbn)
{
	struct sahara_pkt *pkt;
	struct pollfd pfd;
	char buf[4096];
	char tmp[32];
	bool done = false;
	int ret;
	int n;

	while (!done) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		n = poll(&pfd, 1, -1);
		if (n < 0) {
			warn("failed to poll");
			break;
		}

		n = read(fd, buf, sizeof(buf));
		if (n == 0) {
			continue;
		} else if (n < 0) {
			warn("failed to read");
			break;
		}

		pkt = (struct sahara_pkt*)buf;
		if (n != pkt->length) {
			fprintf(stderr, "length not matching");
			return -EINVAL;
		}

		switch (pkt->cmd) {
		case 1:
			sahara_hello(fd, pkt);
			break;
		case 3:
			sahara_read(fd, pkt, prog_mbn);
			break;
		case 4:
			sahara_eoi(fd, pkt);
			break;
		case 6:
			ret = sahara_done(fd, pkt);
			if (ret)
				done = true;
			break;
		case 0x12:
			sahara_read64(fd, pkt, prog_mbn);
			break;
		default:
			sprintf(tmp, "CMD%x", pkt->cmd);
			print_hex_dump(tmp, buf, n);
			break;
		}
	}

	return 0;
}
