# Reading and writing raw binaries

In addition to flashing builds using their XML-based descriptions, QDL supports
reading and writing binaries directly.

```bash
qdl prog_firehose_ddr.elf [read | write] [address specifier] <binary>...
qdl prog_firehose_ddr.elf [erase | sha256] [address specifier]...
```

`erase` wipes the addressed region and `sha256` prints the SHA256 digest the
device computes over it, which is a quick way to verify a write. Multiple
read, write, erase and sha256 commands can be specified at once. The
***address specifier*** can take the forms:

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
