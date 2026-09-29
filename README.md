# Qualcomm Download

[![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)
[![Build on push](https://github.com/linux-msm/qdl/actions/workflows/build.yml/badge.svg)](https://github.com/linux-msm/qdl/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/linux-msm/qdl?sort=semver)](https://github.com/linux-msm/qdl/releases/latest)
[![Debian package](https://img.shields.io/debian/v/qdl/unstable?logo=debian&label=Debian)](https://tracker.debian.org/pkg/qdl)
[![Packaging status](https://repology.org/badge/tiny-repos/qdl.svg)](https://repology.org/project/qdl/versions)

This tool communicates with Qualcomm EDL USB devices (Vendor ID `05c6`) to
upload a flash loader and use it to flash images. Any Qualcomm device that
exposes a vendor-specific EDL interface is accepted; the Product ID (commonly
`9008` for Firehose and `900e` for crash dumps) is not used for matching, as
new devices keep appearing with new IDs.

---

## Build

### Linux

```bash
sudo apt install libxml2-dev libusb-1.0-0-dev libzip-dev libcmocka-dev \
    meson ninja-build help2man
meson setup build
meson compile -C build
```

### MacOS

For Homebrew users:

```bash
brew install libxml2 libusb libzip cmocka meson ninja help2man
meson setup build
meson compile -C build
```

For MacPorts users:

```bash
sudo port install libxml2 libusb libzip cmocka meson ninja help2man
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
pacman -S mingw-w64-x86_64-cmocka
```

Then use the `meson` tool to build QDL:

```bash
meson setup build
meson compile -C build
```

### Build options

Optional parts of QDL are controlled by meson feature options
(`enabled`, `disabled` or `auto`):

- `zip-container` (default `enabled`) - flashing directly from zip archives
  and the `create-zip` subcommand. Requires libzip; configuring fails if it
  is missing. To build a leaner QDL without it, explicitly disable the
  feature:

  ```bash
  meson setup build -Dzip-container=disabled
  ```

- `tests` (default `auto`) - the cmocka-based unit test suite. When cmocka
  is not installed the unit tests are silently skipped; pass
  `-Dtests=enabled` to make a missing cmocka a configure error.

- `nbdkit` (default `auto`) - the nbdkit plugin described in
  [docs/nbd.md](docs/nbd.md). Requires the nbdkit development files.

---

## Use QDL

### EDL mode

The device intended for flashing must be booted into **Emergency Download (EDL)**
mode. EDL is a special boot mode available on Qualcomm-based devices that provides
low-level access for firmware flashing and recovery. It bypasses the standard boot
process, allowing operations such as flashing firmware even on unresponsive devices
or those with locked bootloaders.

Please consult your device's documentation for instructions on how to enter EDL mode.

### Device backends

QDL can reach the EDL device through two backends, selected with
`--backend`:

- `usb` - talks to the device directly through libusb. On Windows this
  requires the device to be bound to the WinUSB driver (for example with
  Zadig) instead of the Qualcomm driver.
- `qud` - Windows only. Talks to the COM port exposed by the official
  Qualcomm QDLoader 9008 driver, so no driver replacement is needed.
- `auto` (default) - polls both backends and uses whichever reaches an EDL
  device first.

To see the EDL devices visible through either backend, together with their
serial numbers, run:

```bash
qdl list
```

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

Other options that commonly matter when flashing:

- `--storage=<emmc|nand|nvme|spinor|ufs>` selects the target storage type
  passed to the programmer, and `--slot=N` the storage slot on targets with
  several devices of the same type.
- `--include=DIR` adds a folder to search for the images referenced by the
  XML files, and `--allow-missing` skips images that cannot be found instead
  of aborting.
- `--skipblock=sha256` asks the device for a SHA256 digest of each region
  about to be written and skips regions whose contents already match, which
  makes reflashing an unchanged build much faster.
- `--skip-reset` leaves the device in EDL mode after flashing instead of
  sending the final reset.
- `--finalize-provisioning` is required, together with a matching UFS
  provisioning XML, to perform irreversible UFS provisioning.

A few maintenance commands do not need a programmer at all and only speak
Sahara to the device: `qdl chipinfo` prints the chip identification the
device reports, and `qdl reset` reboots a device stuck in EDL mode.

### Flashing installer packages

Builds shipped as an installer package (a zip archive or an unpacked
`flashmap.json`) or described by a *contents.xml* file are flashed with the
*flash* subcommand instead of listing the programmer and XML files by hand:

```bash
qdl flash <installer.zip>
qdl flash flashmap.json
qdl flash contents.xml
```

When a package or contents file covers several storage types, layouts or
flavors, a `::specifier` suffix selects what to flash. The selector syntax
and the `create-zip` subcommand that produces installer packages are
described in [docs/installer-packages.md](docs/installer-packages.md).

### Flash simulation (dry run)

Use the `--dry-run` option to run QDL without connecting to or flashing any
device. This is useful for validating your XML descriptors and programmer
arguments, or for generating VIP digest tables (see
[docs/vip.md](docs/vip.md)):

```bash
qdl --dry-run prog_firehose_ddr.elf rawprogram*.xml patch*.xml
```

---

## Documentation

The less common workflows are described in separate guides under
[docs/](docs/):

- [Installer packages and contents.xml](docs/installer-packages.md) -
  flashing zip packages, `flashmap.json` and *contents.xml* builds, the
  storage, layout and flavor selectors, and creating packages with
  `create-zip`.
- [Reading and writing raw binaries](docs/raw-io.md) - `read`, `write`,
  `erase` and `sha256` on physical partitions, sector ranges and named GPT
  partitions.
- [Validated Image Programming](docs/vip.md) - generating and signing
  digest tables for Secure Boot targets and validating them without
  hardware.
- [Multi-programmer targets](docs/multi-programmer.md) - targets that
  request several Sahara images: command-line image lists, Sahara
  configuration XML files and programmer archives.
- [Collect crash dump](docs/ramdump.md) - collecting memory segments from
  a crashed target with `qdl ramdump`.
- [Sahara kickstart](docs/kickstart.md) - loading firmware into
  flashless-boot devices such as the Cloud AI 100 with `qdl ks`.
- [Flashing from WSL2](docs/wsl2.md) - forwarding the EDL device into WSL2
  with usbipd-win and re-attaching it after re-enumeration.
- [nbdkit plugin](docs/nbd.md) - exposing a physical partition as a block
  device on the host.

---

## Run tests

The test suite is run with the `meson` tool:

```bash
meson test -C build
```

Tests are grouped into suites, selectable with `--suite`:

- `unit` - cmocka programs covering the XML, JSON, contents and archive
  parsers. Only built when cmocka was found at configure time (see
  [Build options](#build-options)).
- `integration` - scripts that drive the built `qdl` binary without a
  device, for example VIP table and Sahara archive generation.
- `hil` and `hil-vip` - hardware-in-the-loop steps that flash and read back
  an attached EDL device. They are skipped unless `QDL_HIL_BUILD`,
  `QDL_HIL_STORAGE` and friends are set; see the comments at the top of
  [tests/test_hil.sh](tests/test_hil.sh) for the full environment.

A plain `meson test` runs the unit and integration suites. To run only one
suite, for example the unit tests:

```bash
meson test -C build --suite unit
```

---

## Generate man pages

Manpages can be generated using `manpages` target:

```bash
meson compile manpages -C build
```

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the coding style, the checkpatch and
markdown-lint targets, and how to submit pull requests.

---

## License

This tool is licensed under the BSD 3-Clause license. Check out [LICENSE](LICENSE)
for more details.
