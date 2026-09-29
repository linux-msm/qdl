# Validated Image Programming (VIP)

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

## Validating VIP tables without hardware

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
