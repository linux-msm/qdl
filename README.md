# Qualcomm Download

[![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)
[![Build on push](https://github.com/linux-msm/qdl/actions/workflows/build.yml/badge.svg)](https://github.com/linux-msm/qdl/actions/workflows/build.yml/badge.svg)

This tool communicates with Qualcomm EDL USB devices (Vendor ID `05c6`, Product
IDs `9008`, `900e`, `901d`) to upload a flash loader and use it to flash images.

## Build

### Linux

```bash
sudo apt install libxml2-dev libusb-1.0-0-dev libzip-dev meson ninja-build help2man
meson setup build
meson compile -C build
```

### MacOS

For Homebrew users:

```bash
brew install libxml2 libusb libzip meson ninja help2man
meson setup build
meson compile -C build
```

For MacPorts users:

```bash
sudo port install libxml2 libusb libzip meson ninja help2man
meson setup build
meson compile -C build
```

### Windows

First, install the [MSYS2 environment](https://www.msys2.org/). Then, run the
MSYS2 MinGW64 terminal (located at `<msys2-installation-path>\mingw64.exe`) and
install additional packages needed for QDL compilation using the `pacman` tool:

```bash
pacman -S base-devel --needed
pacman -S git
pacman -S help2man
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-meson
pacman -S mingw-w64-x86_64-ninja
pacman -S mingw-w64-x86_64-libusb
pacman -S mingw-w64-x86_64-libxml2
pacman -S mingw-w64-x86_64-libzip
```

Then use the `meson` tool to build QDL:

```bash
meson setup build
meson compile -C build
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
qdl prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

If you have multiple boards connected to the host, provide the serial number of
the board to flash through the `--serial` option:

```bash
qdl --serial=0AA94EFD prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

### Flash simulation (dry run)

Use the `--dry-run` option to run QDL without connecting to or flashing any
device. This is useful for validating your XML descriptors and programmer
arguments, or for generating VIP digest tables (see below):

```bash
qdl --dry-run prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

### Reading and writing raw binaries

In addition to flashing builds using their XML-based descriptions, QDL supports
reading and writing binaries directly.

```bash
qdl prog_firehose_ddr.elf [read | write] [address specifier] <binary>...
```

Multiple read and write commands can be specified at once. The ***address
specifier*** can take the forms:

- N - single number, specifies the physical partition number N, starting at
  sector 0. To read data, the number of sectors must be specified explicitly
  using the N/S+L form.

- N/S - two numbers, specifies the physical partition number N and the start
  sector S. To read data, the number of sectors must be specified explicitly
  using the N/S+L form.

- N/S+L - three numbers, specifies the physical partition number N, the start
  sector S and the number of sectors L, that ***binary*** should be written to,
  or which should be read into ***binary***.

- partition name - a string, will match against partition names across the GPT
  partition tables on all physical partitions.

- N/partition_name - a number followed by a string, will match against
  partition names of the GPT partition table in the specified physical
  partition N.

### Validated Image Programming (VIP)

QDL supports **Validated Image Programming (VIP)** mode, which is activated
when Secure Boot is enabled on the target. VIP controls which packets are
allowed to be issued to the target by hashing all received data and comparing
each resulting digest against the next entry in a pre-loaded digest table.
If the digest matches, the packet is accepted; otherwise, the packet is
rejected, and the target halts.

To use VIP programming, a digest table must be generated prior to flashing the device.
To generate a table of digests, run QDL with the `--create-digests` option,
providing a path to store the VIP tables. Note that `--create-digests`
implicitly enables dry-run mode, so no device connection is required:

```bash
mkdir vip
qdl --create-digests=./vip prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

As a result, three types of files are generated:

- `DIGEST_TABLE.bin` - contains the SHA256 table of digests for all Firehose
  packets to be sent to the target. It is an intermediary table and is
  used only for the subsequent generation of `DigestsToSign.bin` and
  `ChainedTableOfDigests<n>.bin` files, and is not used directly by QDL for
  VIP programming.

- `DigestsToSign.bin` - first 53 digests + SHA256 hash of `ChainedTableOfDigests0.bin`.
  This file must be converted to MBN format and then signed with sectools:

  ```bash
  sectools mbn-tool generate --data DigestsToSign.bin --mbn-version 6 --outfile DigestsToSign.bin.mbn
  sectools secure-image --sign DigestsToSign.bin.mbn --image-id=VIP
  ```

  Please check the security profile for your SoC to determine which version of
  the MBN format should be used.

- `ChainedTableOfDigests<n>.bin` - contains the remaining digests, split across
  multiple files of up to 255 digests each. Non-final files have the SHA256
  hash of the next chained table appended. The final file has a trailing zero
  byte appended to ensure its size is not a multiple of the sector size.

To flash a board using VIP mode, provide the path where the previously generated
and signed tables are stored using the `--vip-table-path` option:

```bash
qdl --vip-table-path=./vip prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

Note that `--vip-table-path` and `--create-digests` are mutually exclusive.

#### Validating VIP tables without hardware

Before flashing a real device it is possible to verify that the signed digest
tables match the data that will be sent, using `--dry-run` together with
`--vip-table-path`:

```bash
qdl --dry-run --vip-table-path=./vip prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

QDL will simulate the full Firehose session, compute SHA256 over every packet
it would send, and compare each hash against the corresponding entry in the
loaded digest tables. Any mismatch is reported to stderr. All mismatches are
printed before the run exits so that every problem is visible at once.

This catches table/data mismatches early -- before committing to a real flash --
and is useful both as a local sanity check and as a step in CI pipelines.

### Multi-programmer targets

On some targets multiple files need to be loaded in order to reach the
Firehose programmer; these targets will request multiple images over Sahara.
Three mechanisms for providing these images are provided:

#### Command line argument

The *programmer* argument allows specifying a comma-separated list of
colon-separated "id" and "filename" pairs. Each filename should refer to the
Sahara image of the specified Sahara image id.

```bash
qdl 13:prog_firehose_ddr.elf,42:the-answer rawprogram.xml
```

#### Sahara configuration XML file

Flattened METAs include the various images that need to be loaded to
enter Firehose mode, as well as a sahara_config XML file, which defines the
Sahara image id for each of these images.

If the specified device programmer is determined to be a Sahara configuration
XML file, it will be parsed and the referenced files will be loaded and
serviced to the device upon request.

```bash
qdl sahara_programmer.xml rawprogram.xml
```

#### Programmer archive

Directly providing a list of ids and filenames is cumbersome and error-prone,
so QDL accepts a "*programmer archive*". This allows the user to use the
tool in the same fashion as was done for single-programmer targets.

The *programmer archive* is a CPIO archive containing the Sahara images to be
loaded, identified by the filename **id[:filename]** (*filename* is optional,
but useful for debugging). Each included file will be used to serve requests
for the given Sahara *id*.

Such an archive can be created by putting the target's programmer images in an
empty directory, then executing the following command from that directory:

```bash
ls | cpio -o -H newc > ../programmer.cpio
```

*programmer.cpio* can now be passed to QDL and the included images will be
served in order to reach Firehose mode.

## Collect crash dump

When a Qualcomm target crashes or is forced into crash dump mode, the
bootloader re-enumerates the device over USB with Product ID `900e` and
offers memory segments for collection via the Sahara protocol.

A kernel crash can be triggered on the target with:

```bash
echo c > /proc/sysrq-trigger
```

Use `qdl ramdump` on the host to collect the dump:

```bash
qdl ramdump -o ./ramdump
```

This writes each offered memory segment to a separate file under `./ramdump`.
To collect only specific segments, pass a comma-separated filter:

```bash
qdl ramdump -o ./ramdump OCIMEM,CODERAM
```

## Run tests

To run the integration test suite for QDL, use the `meson` tool with `test`
param:

```bash
meson test -C build
```

## Generate man pages

Manpages can be generated using `manpages` target:

```bash
meson compile manpages -C build
```

## Contributing

Please submit any patches to the qdl (`master` branch) by using the GitHub pull
request feature. Fork the repo, create a branch, do the work, rebase with upstream,
and submit the pull request.

The preferred coding style for this tool is [Linux kernel coding style](https://www.kernel.org/doc/html/v6.15/process/coding-style.html).

Before creating a commit, please ensure that your changes adhere to the coding style
by using the `meson compile check-cached -C build` target, for example:

```bash
$ git status
On branch improvements
Changes to be committed:
  (use "git restore --staged <file>..." to unstage)
  modified:   qdl.c
  modified:   qdl.h

$ meson compile check-cached -C build
[0/1] Running external command check-cached (wrapped by meson to set env)
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

This tool is licensed under the BSD 3-Clause license. Check out [LICENSE](LICENSE)
for more details.
