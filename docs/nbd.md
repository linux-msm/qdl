# nbdkit plugin

In addition to the `qdl` programmer, an
[nbdkit](https://gitlab.com/nbdkit/nbdkit) plugin can be built. It uploads
the firehose programmer and exposes a physical partition (LUN) as a block
device on the host, so its partition table can be edited and its partitions
mounted with ordinary tools.

## Building the plugin

The plugin is built by meson only when the `nbdkit` development files are
present. Install the runtime and the development package, then reconfigure:

- Debian/Ubuntu: `sudo apt install nbdkit nbdkit-dev`
- Fedora: `sudo dnf install nbdkit nbdkit-devel`

```bash
meson setup --reconfigure build
meson compile -C build
```

`meson setup` reports `Run-time dependency nbdkit found: YES` and produces
`build/nbdkit-qdl-plugin.so`. Pass `-Dnbdkit=enabled` to make the plugin
mandatory (the build fails if nbdkit is missing) instead of the default
auto-detection.

## Plugin parameters

- `programmer` - firehose programmer to upload (required)
- `lun` - physical partition (LUN) to expose, defaults to `1`
- `storage` - `ufs` or `emmc`, selects the firehose configuration
- `debug` - set to `1` for verbose qdl logging

## Serving a LUN as a block device

The device must be in EDL mode, exactly as for a normal `qdl` run.

**1. Start nbdkit.** In one terminal, launch the server. `-f` keeps it in the
foreground and `-v` prints a debug log:

```bash
sudo nbdkit -fv ./build/nbdkit-qdl-plugin.so \
    programmer=prog_firehose_ddr.elf lun=0
```

nbdkit now listens on TCP port 10809 (`nbd://localhost`). It does not touch
the device yet: the programmer is uploaded and firehose is configured lazily,
the first time a client connects, and kept configured until nbdkit exits.

**2. Confirm the export.** In a second terminal, query it. This triggers the
one-time device setup and prints the LUN's size, so it is the quickest check
that the plugin can talk to the board:

```bash
nbdinfo nbd://localhost
```

**3. Attach a kernel block device.** Load the `nbd` module and bind the export
to a `/dev/nbdN` node with `nbd-client`. `-N default` selects the plugin's
default export, `10809` is the port:

```bash
sudo modprobe nbd
sudo nbd-client -N default localhost 10809 /dev/nbd0
```

`/dev/nbd0` now mirrors the LUN. `nbd-client` prints the negotiated size on
connect.

**4. Read the LUN.** Query its size (in bytes) and read from it with any
standard tool:

```bash
sudo blockdev --getsize64 /dev/nbd0
sudo dd if=/dev/nbd0 of=/tmp/lun0.head bs=1M count=1
```

You can partition, mount, `dd`, or image `/dev/nbd0` like any other disk.

**5. Detach when done**, then stop nbdkit (Ctrl-C in its terminal):

```bash
sudo nbd-client -d /dev/nbd0
```

> [!WARNING]
> Writing to `/dev/nbd0` writes straight to the device's flash. Firehose
> reads are also slow - every request is a separate firehose command - so
> large transfers take a while; a bigger block size (`bs=8M`) helps.
