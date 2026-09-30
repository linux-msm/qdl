# Sahara kickstart for flashless-boot devices (qdl ks)

The `qdl ks` ("kickstart") subcommand uses the Sahara protocol to load
images from the host to the device. It targets *flashless boot* devices
such as the Qualcomm Cloud AI 100, which fetch their runtime firmware
from the host on every boot rather than storing it on-device.

Unlike normal flashing, kickstart stops once Sahara is done: no Firehose
programmer is uploaded and nothing is written to storage.

One argument is required: `-s id:path` registers an image mapping. It
may be specified more than once, one mapping per Sahara image id the
device may request.

By default the device is found through the same backends the other
subcommands use, so `--backend` and `--serial` select it just as they do
when flashing:

```bash
qdl ks -s 13:prog_firehose_ddr.elf
```

Devices that a kernel driver exposes as a node instead - such as the MHI
Sahara endpoints - are addressed with `-p`, which is driven with plain
open/read/write operations rather than through a backend:

```bash
qdl ks -p /dev/mhi0_QAIC_SAHARA \
       -s 1:/opt/qti-aic/firmware/fw1.bin \
       -s 2:/opt/qti-aic/firmware/fw2.bin
```

Because `-p` names the transport outright, it cannot be combined with
`--serial` or `--backend`.

The mapped files do not need to exist at invocation time. If `qdl ks`
cannot open a requested file, the device decides the next action. This
makes it possible to wire `qdl ks` into a single udev rule that covers
multiple device configurations (for example, an optional DDR training
image that is only present on some setups).
