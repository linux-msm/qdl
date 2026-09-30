# Collect crash dump

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
