# SPDX-License-Identifier: BSD-3-Clause
#!/bin/sh

SCRIPT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
OUTDIR="${1:-$SCRIPT_PATH}"

mkdir -p "$OUTDIR"

create_file_with_size() {
	filename="$1"
	size_kbytes="$2"

	dd if=/dev/zero of="$OUTDIR/$filename" bs=1024 count="$size_kbytes" status=none
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

# Copy static test data (XML descriptors and flashmap.json) into the output directory
if [ "$OUTDIR" != "$SCRIPT_PATH" ]; then
	cp "$SCRIPT_PATH"/*.xml "$OUTDIR"/
	cp "$SCRIPT_PATH"/flashmap.json "$OUTDIR"/
fi

(cd $OUTDIR ; zip flashmap.zip prog_firehose_ddr.elf efi.bin gpt_backup0.bin \
		  gpt_backup1.bin gpt_main0.bin gpt_main1.bin rootfs.img \
		  xbl_config.elf xbl.elf rawprogram0.xml rawprogram1.xml \
		  patch0.xml patch1.xml flashmap.json)
