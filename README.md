# Qualcomm Download

[![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)
[![Build on push](https://github.com/linux-msm/qdl/actions/workflows/build.yml/badge.svg)](https://github.com/linux-msm/qdl/actions/workflows/build.yml/badge.svg)

This tool communicates with USB devices of id `05c6:9008` to upload a flash
loader and use this to flash images.

## Build

### Linux

```bash
sudo apt install libxml2 libusb-1.0-0-dev
make
```

Manpages can be built separately:

```bash
sudo apt install help2man
make manpages
```

### MacOS

For Homebrew users,

```bash
brew install libxml2 pkg-config libusb
make
```

For MacPorts users

```bash
sudo port install libxml2 pkgconfig libusb
make
```

### Windows

First, install the [MSYS2 environment](https://www.msys2.org/). Then, run the
MSYS2 MinGW64 terminal (located at `<msys2-installation-path>\mingw64.exe`) and
install additional packages needed for QDL compilation using the `pacman` tool:

```bash
pacman -S base-devel --needed
pacman -S git
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-make
pacman -S mingw-w64-x86_64-pkg-config
pacman -S mingw-w64-x86_64-libusb
pacman -S mingw-w64-x86_64-libxml2
```

Then use the `make` tool to build QDL:

```bash
make
```

## Use QDL

### EDL mode

The device intended for flashing must be booted into **Emergency Download (EDL)**
mode. EDL is a special boot mode available on Qualcomm-based devices that provides
low-level access for firmware flashing and recovery. It bypasses the standard boot
process, allowing operations such as flashing firmware even on unresponsive devices
or those with locked bootloaders.

Please consult your device’s documentation for instructions on how to enter EDL mode.

### Flash device

Run QDL with the `--help` option to view detailed usage information.

Below is an example of how to invoke QDL to flash a FLAT build:

```bash
qdl --dry-run prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

If you have multiple boards connected the host, provide the serial number of
the board to flash through `--serial` param:

```bash
qdl --serial=0AA94EFD prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

### Reading and writing raw binaries

In addition to flashing builds using their XML-based descriptions, QDL supports
reading and writing binaries directly.

```bash
qdl prog_firehose_ddr.elf [read | write] [address specifier] <binary>...
```

Multiple read and write commands can be specified at once. The ***address
specifier*** can take the forms:

- N - single number, specifies the physical partition number N to write the
  ***binary** into, starting at sector 0 (currently reading a whole physical
  partition is not supported).

- N/S - two numbers, specifies the physical partition number N, and the start
  sector S, to write the ***binary*** into (reading with an offset is not
  supported)

- N/S+L - three numbers, specified the physical partition number N, the start
  sector S and the number of sectors L, that ***binary*** should be written to,
  or which should be read into ***binary***.

- partition name - a string, will match against partition names across the GPT
  partition tables on all physical partitions.

- N/partition_name - single number, followed by string - will match against
  partition names of the GPT partition table in the specified physical
  partition N.

### Validated Image Programming (VIP)

QDL now supports **Validated Image Programming (VIP)** mode , which is activated
when Secure Boot is enabled on the target. VIP controls which packets are allowed
to be issued to the target. Controlling the packets that can be sent to the target
is done through hashing. The target applies a hashing function to all received data,
comparing the resulting hash digest against an existing digest table in memory.
If the calculated hash digest matches the next entry in the table, the packet
(data or command) is accepted; otherwise, the packet is rejected,and the target halts.

To use VIP programming, a digest table must be generated prior to flashing the device.
To generate table of digests run QDL with `--create-digests` param,
providing a path to store VIP tables. For example:

```bash
mkdir vip
qdl --create-digests=./vip prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

As a result 3 types of files are generated:

- `DIGEST_TABLE.bin` - contains the SHA256 table of digests for all firehose
  packets to be sent to the target. It is an intermediary table and is
  used only for the subsequent generation of `DigestsToSign.bin` and
  `ChainedTableOfDigests\<n\>.bin` files. It is not used by QDL for VIP
  programming.

- `DigestsToSign.bin` - first 53 digests + digest of `ChainedTableOfDigests.bin`.
  This file has to be converted to MBN format and then signed with sectools:

  ```bash
  sectools mbn-tool generate --data DigestsToSign.bin --mbn-version 6 --outfile DigestsToSign.bin.mbn
  sectools secure-image --sign DigestsToSign.bin.mbn --image-id=VIP
  ```

  Please check the security profile for your SoC to determine which version of
  the MBN format should be used.

- `ChainedTableOfDigests\<n\>.bin` - contains left digests, split on
  multiple files with 255 digests + appended hash of next table.

To flash board using VIP mode provide a path where previously generated and signed
table of digests are stored using `--vip-table-path` param:

```bash
qdl --vip-table-path=./vip prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

### Multi-programmer targets

On some targets multiple files need to be loaded in order to reach the
Firehose programmer, these targets will request multiple images over Sahara.
Three mechanisms for providing these images are provided:

#### Command line argument

The *programmer* argument, allows specifying a comma-separated list of
colon-separated "id" and "filename" pairs. Each filename should refer to the
Sahara image of the specified Sahara image id.

```bash
qdl 13:prog_firehose_ddr.elf,42:the-answer rawprogram.xml
```

#### Sahara configuration XML file

Flattened METAs does include the various images that need to be loaded to
enter Firehose mode, as well as a sahara_config XML file, which defines the
Sahara image id for each of these images.

If the specified device programmer is determined to be a Sahara configuration
XML file, it will be parsed and the referred to files will be loaded and
serviced to the device upon request.

```bash
qdl sahara_programmer.xml rawprogram.xml
```

#### Programmer archive

Directly providing a list of ids and filenames is cumbersome and error prone,
QDL therefore accepts a "*programmer archive*". This allows the user to use the
tool in the same fashion as was done for single-programmer targets.

The *programmer archive* is a CPIO archive containing the Sahara images to be
loaded, identified by the filename **id[:filename]** (*filename* is optional,
but useful for debugging). Each included file will be used to serve requests
for the given Sahara *id*.

Such an archive can be created by putting the target's programmer images in an
empty directory, then in that directory execute the command:

```bash
ls | cpio -o -H newc > ../programmer.cpio
```

*programmer.cpio* can now be passed to QDL and the included images will be
served, in order to reach Firehose mode.

## Run tests

To run the integration test suite for QDL, use the `make tests` target:

```bash
make tests
```

## Contributing

Please submit any patches to the qdl (`master` branch) by using the GitHub pull
request feature. Fork the repo, create a branch, do the work, rebase with upstream,
and submit the pull request.

The preferred coding style for this tool is [Linux kernel coding style](https://www.kernel.org/doc/html/v6.15/process/coding-style.html).

Before creating a commit, please ensure that your changes adhere to the coding style
by using the `make check-cached` target, for example:

```bash
$ git status
On branch improvements
Changes to be committed:
  (use "git restore --staged <file>..." to unstage)
  modified:   qdl.c
  modified:   qdl.h

$ make check-cached
Running checkpatch on staged changes...
ERROR: trailing whitespace
#28: FILE: qdl.h:32:
+^IQDL_DEVICE_USB,   $

total: 1 errors, 0 warnings, 0 checks, 27 lines checked

NOTE: For some of the reported defects, checkpatch may be able to
      mechanically convert to the typical style using --fix or --fix-inplace.

NOTE: Whitespace errors detected.
      You may wish to use scripts/cleanpatch or scripts/cleanfile

Your patch has style problems, please review.
```

## License

This tool is licensed under the BSD 3-Clause licensed. Check out [LICENSE](LICENSE)
for more detais.
