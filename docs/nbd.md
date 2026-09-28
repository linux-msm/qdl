# nbdkit plugin

In addition to the `qdl` programmer, an
[nbdkit](https://gitlab.com/nbdkit/nbdkit) plugin can be built. It uploads
the firehose programmer and exposes a physical partition (LUN) as a block
device on the host, so its partition table can be edited and its partitions
mounted with ordinary tools.

## Building the plugin

The plugin is only built when the `nbdkit` development library is present.

Install the development package:

- Debian/Ubuntu: `sudo apt install nbdkit-plugin-dev`
- Fedora: `sudo dnf install nbdkit-devel`

Then reconfigure and build:

```bash
meson setup --reconfigure build
meson compile -C build
```

Ensure `meson setup` reports `Run-time dependency nbdkit found: YES` and
produces `build/nbdkit-qdl-plugin.so`. Pass `-Dnbdkit=enabled` to `meson setup`
to make the plugin build mandatory (the build will fail if the `nbdkit`
development files are missing) instead of the default auto-detection.

## Plugin parameters

The plugin can be ran with `nbdkit`; see the example below for details.

- `programmer` - path to the Firehose loader to upload (required; may be an
  absolute path or a path relative to the current working directory)
- `lun` - physical partition (LUN) to expose, defaults to `LUN 0` which usually
  contains the userdata/HLOS partitions
- `storage` - `ufs` or `emmc`, selects the firehose configuration
- `timeout` - seconds to wait for an EDL device when setting the device up,
  defaults to `60`. Use `0` to wait indefinitely, as `qdl` itself does
- `debug` - set to `1` for verbose qdl logging

## Serving a LUN as a block device

**1. Prerequisites.**

Install the following packages:

- Debian/Ubuntu: `sudo apt install libnbd-bin nbd-client nbdkit`
- Fedora: `sudo dnf install libnbd nbd nbdkit`

The device must be in EDL mode and connected to your workstation, exactly as
for a normal `qdl` run.

You must have a copy of the relevant `prog_firehose_ddr.elf` loader in the
working directory, or change the value of the `programmer=` argument in the
following `nbdkit` commands to the absolute or relative path to the loader
binary.

**2. Start nbdkit.**

In one terminal, launch the server. `-f` keeps it in the foreground and `-v`
prints the debug log. You shouldn't need to run `nbdkit` as root if you have the
correct permissions to access the device over USB:

```bash
nbdkit -fv ./build/nbdkit-qdl-plugin.so \
    programmer=prog_firehose_ddr.elf lun=0
```

`nbdkit` listens on TCP port 10809 (`nbd://localhost`). It does not connect
to the device yet: once the first client connects to the plugin, the programmer
binary is uploaded to the device, firehose is configured and the device will
stay in firehose mode until `nbdkit` exits.

**3. Confirm the export.**

In a second terminal, connect to the NBD server with `nbdinfo` to display
information about the block device. When this client connects to the server,
the `nbdkit` plugin attempts to connect to a device in EDL mode, uploads the
programmer and then communicates with the device in firehose mode until
`nbdkit` exits.

Information about the device, including the LUN's physical block size and block
count, is read during the initial connection and is used to calculate the size
of the block device. This is cached in the NBD server and passed back to
`nbdinfo` which prints it to the console, making this a quick check of
communication with the device:

```bash
nbdinfo nbd://localhost
```

This prints (among other metadata) the overall size of the LUN and the sector
size, for example:

```
export-size: 124646326272 (118872M)
block_size_preferred: 4096
```

But, if a connection to the EDL device cannot be established within `timeout`
seconds, the NBD client connection fails. The server keeps running and waits
for another NBD client to connect, which then retries connecting to a device in
EDL mode.

**4. Confirm the device's sector size.**

Devices with UFS storage have a 4096-byte sector size and devices with eMMC
storage have a 512-byte sector size. Unfortunately, `nbd-client` defaults to
assuming all devices have a 512-byte sector size and does not automatically
detect the sector size reported by the server.

The device's sector size is printed to the `nbdkit` debug log after the
initial firehose connection:

`nbdkit: qdl[1]: debug: serving LUN 0; sector size 4096 bytes, 30431232 sectors`

The sector size can also be read from the output of `nbdinfo nbd://localhost`,
which can be accessed from the client, without reading the `nbdkit` debug log.
A sample output, showing this device has `4096` sectors:

`block_size_preferred: 4096`

**5. Attach a kernel block device.**

Load the `nbd` module and bind the export to a `/dev/nbdN` node using
`nbd-client`. `-b 4096` selects the sector size (default `512`); `-N default`
selects the plugin's default export, `10809` is the port:

```bash
sudo modprobe nbd
sudo nbd-client -b 4096 -N default localhost 10809 /dev/nbd0
```

`/dev/nbd0` now mirrors the LUN. `nbd-client` prints the negotiated size on
connect.

**6. Read/write data to the device's LUN.**

Query its size (in bytes) and read from it with any standard tool:

```bash
sudo blockdev --getsize64 /dev/nbd0
sudo dd if=/dev/nbd0 of=/tmp/lun0.head bs=1M count=1
```

You can partition, mount, `dd`, or image `/dev/nbd0` like any other disk.

> [!WARNING]
> If you get any errors about reading partition tables, make sure to check
> you have chosen the correct sector size for the `nbd-client` command above.
> Also, writing to `/dev/nbd0` writes straight to the device's flash. Firehose
> reads are also slow - every request is a separate firehose command - so
> large transfers take a while; a bigger block size (e.g. `bs=8M`) helps.

**7. Disconnection.**

First disconnect the `nbd-client` from the server:

```bash
sudo nbd-client -d /dev/nbd0
```

Then stop `nbdkit` by pressing Ctrl-C in its terminal.

Before `nbdkit` shuts down, it resets the device.
