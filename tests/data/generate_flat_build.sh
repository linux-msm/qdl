#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause

SCRIPT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

create_file_with_size() {
	filename="$1"
	size_kbytes="$2"

	dd if=/dev/zero of="$SCRIPT_PATH/$filename" bs=1024 count="$size_kbytes" status=none
}

create_file_with_size prog_firehose_ddr.elf 20
create_file_with_size efi.bin 524288
create_file_with_size gpt_backup0.bin 20
create_file_with_size gpt_backup1.bin 20
create_file_with_size gpt_main0.bin 24
create_file_with_size gpt_main1.bin 24
create_file_with_size rootfs.img 512000
create_file_with_size xbl_config.elf 320
create_file_with_size xbl.elf 800
