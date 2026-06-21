# Qualcomm Download

[![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)
[![Build on push](https://github.com/linux-msm/qdl/actions/workflows/build.yml/badge.svg)](https://github.com/linux-msm/qdl/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/linux-msm/qdl?sort=semver)](https://github.com/linux-msm/qdl/releases/latest)
[![Debian package](https://img.shields.io/debian/v/qdl/unstable?logo=debian&label=Debian)](https://tracker.debian.org/pkg/qdl)
[![Packaging status](https://repology.org/badge/tiny-repos/qdl.svg)](https://repology.org/project/qdl/versions)

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

### Flashing from WSL2 (usbipd-win)

> [!WARNING]
> This is unreliable with current usbipd-win.

See:

- <https://github.com/dorssel/usbipd-win/issues/924>
- <https://github.com/dorssel/usbipd-win/issues/1022>
- <https://github.com/dorssel/usbipd-win/issues/1067>

On Windows, QDL can run inside a WSL2 distribution, but WSL2 does not see USB
devices by default — they must be forwarded from the Windows host with
[usbipd-win](https://github.com/dorssel/usbipd-win). Install it on the Windows
side (`winget install usbipd`), then forward the EDL device into WSL.

EDL devices enumerate under Vendor ID `05c6` with Product ID `9008` (Firehose),
`900e` (crash dump), or `901d`. From a Windows terminal, list the connected
devices and note the **BUSID** of the EDL device:

```powershell
usbipd list
```

Each device must be *bound* once before it can be attached. Binding requires an
**elevated (Administrator)** PowerShell, but it persists across reboots, so this
is a one-time step per device:

```powershell
usbipd bind --busid <BUSID>
```

Attaching the device to WSL does **not** require Administrator and can be run
from inside WSL by calling the Windows binary:

```bash
usbipd.exe attach --wsl --busid <BUSID>
```

Once attached, the device appears in WSL (check with `lsusb` for `05c6:9008`)
and QDL can flash it as usual.

#### Re-attaching after EDL re-enumeration

The EDL device is a USB gadget that only exists while the board is in EDL mode.
Whenever the board re-enters EDL — including after QDL finishes and the target
reboots — the USB device is torn down and re-created. The `bind` persists, but
the **attach is dropped**, and the host may assign a **different BUSID** than
before. A QDL run that reports no device found is almost always this: the device
simply needs to be re-attached.

After each entry into EDL, re-detect the BUSID and attach again. This can be
scripted from WSL so the BUSID does not have to be looked up by hand:

```bash
BUSID=$(usbipd.exe list | grep -iE '9008|900e|901d' | awk '{print $1}' | tr -d '\r')
usbipd.exe attach --wsl --busid "$BUSID"
```

If you flash repeatedly, run this re-attach step before every `qdl` invocation.

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

### Flashing installer packages

If you have an installer package instead of individual binaries and XML
definitions, you can flash this using the *flash* subcommand:

```bash
qdl flash <installer.zip>
```

If the *installer package* is unpacked it can be installed as:

```bash
qdl flash flashmap.json
```

These can of course be combined with e.g. *--serial*.

A subset of the installer package can be selected for installation by appending
a **::storage1[,storage2...]** suffix to the file name.

If a flashmap contains multiple layouts, select the desired layout by appending
**::layout-name**. The layout selector can be combined with storage filters
using **::layout-name/storage1[,storage2...]**, for example:

```bash
qdl flash installer.zip::layout1/ufs
```

### Flashing contents.xml

QDL also supports flashing builds described by *contents.xml* files:

```bash
qdl flash contents.xml
```

As the contents XML can describe the content for multiple storage types and
multiple flavors, it might be necessary to select which content to flash. This
is done by appending the **::specifier1,specifier2...** suffix to the file
name. The specifier is matched against **storage types** and **flavors**. At
most one resolved specifier per storage is allowed, and only the selected parts
are flashed. As an example:

```bash
qdl flash contents.xml::ufs,safe_rtos
```

will flash the UFS storage with the only applicable flavor, and will flash
*safe_rtos* onto the spinor.

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

Each offered memory segment is written to a separate file under
`./ramdump`. To collect only specific segments, pass a comma-separated
filter:

```bash
qdl ramdump -o ./ramdump OCIMEM,CODERAM
```

## Sahara kickstart for flashless-boot devices (qdl ks)

The `qdl ks` ("kickstart") subcommand uses the Sahara protocol to load
images from the host to the device. It targets *flashless boot* devices
such as the Qualcomm Cloud AI 100, which fetch their runtime firmware
from the host on every boot rather than storing it on-device.

Unlike normal flashing, kickstart does not use USB or Firehose; it
talks to a kernel-provided device node using plain open/read/write
operations. Its argument set is correspondingly minimal.

Two arguments are required: `-p` selects the Sahara port (a device node)
and `-s id:path` registers an image mapping. The `-s` option may be
specified more than once, one mapping per Sahara image id the device may
request.

```bash
qdl ks -p /dev/mhi0_QAIC_SAHARA \
       -s 1:/opt/qti-aic/firmware/fw1.bin \
       -s 2:/opt/qti-aic/firmware/fw2.bin
```

The mapped files do not need to exist at invocation time. If `qdl ks`
cannot open a requested file, the device decides the next action. This
makes it possible to wire `qdl ks` into a single udev rule that covers
multiple device configurations (for example, an optional DDR training
image that is only present on some setups).

## Run tests

To run the integration test suite for QDL, use the `meson` tool with `test`
param:

```bash
meson test -C build
```

If `cmocka` is installed at configure time, Meson also builds and runs the
unit test suite (including `program_load_xml` path-resolution tests). You can
run only unit tests with:

```bash
meson test -C build --suite unit
```

## Generate man pages

Manpages can be generated using `manpages` target:

```bash
meson compile manpages -C build
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the coding style, the checkpatch and
markdown-lint targets, and how to submit pull requests.

## License

This tool is licensed under the BSD 3-Clause license. Check out [LICENSE](LICENSE)
for more details.
