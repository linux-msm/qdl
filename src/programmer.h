/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __PROGRAMMER_H__
#define __PROGRAMMER_H__

struct sahara_image;

int decode_programmer(char *s, struct sahara_image *images);
int decode_programmer_archive(struct sahara_image *blob, struct sahara_image *images);
int sahara_archive_write(const char *filename, const struct sahara_image *images);

#endif
