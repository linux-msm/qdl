#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def read_hex(data, offset):
    return int(data[offset:offset + 8], 16)


def parse_newc(filename):
    data = Path(filename).read_bytes()
    pos = 0
    entries = {}

    while True:
        header = data[pos:pos + 110]
        assert len(header) == 110, "truncated archive header"
        assert header[:6] == b"070701", "bad archive magic"

        filesize = read_hex(header, 54)
        namesize = read_hex(header, 94)
        pos += 110

        name = data[pos:pos + namesize]
        assert len(name) == namesize, "truncated archive name"
        assert name[-1:] == b"\0", "unterminated archive name"
        name = name[:-1].decode("ascii")

        pos = align_up(pos + namesize, 4)

        payload = data[pos:pos + filesize]
        assert len(payload) == filesize, "truncated archive payload"
        pos = align_up(pos + filesize, 4)

        if name == "TRAILER!!!":
            break

        entries[name] = payload

    return entries


def run_qdl(qdl, *args, cwd=None, expect_success=True):
    result = subprocess.run([qdl, *args], cwd=cwd, capture_output=True,
                            text=True)

    if expect_success and result.returncode:
        print(result.stdout)
        print(result.stderr, file=sys.stderr)
        raise AssertionError("qdl command failed")

    if not expect_success and not result.returncode:
        print(result.stdout)
        print(result.stderr, file=sys.stderr)
        raise AssertionError("qdl command succeeded unexpectedly")

    return result


def write_payloads(path):
    payloads = {
        "prog.elf": b"programmer\0payload\n",
        "cfg.elf": b"cfg",
        "aux.bin": bytes(range(17)),
    }

    path.mkdir(parents=True, exist_ok=True)
    for name, data in payloads.items():
        (path / name).write_bytes(data)

    return payloads


def write_sahara_xml(filename, payload_dir, prog_name="prog.elf",
                     cfg_name="cfg.elf"):
    filename.write_text(f"""<?xml version="1.0" ?>
<sahara_config>
  <chipset>test</chipset>
  <images>
    <image image_id="13" image_path="{payload_dir / prog_name}"/>
    <image image_id="38" image_path="{payload_dir / cfg_name}"/>
  </images>
</sahara_config>
""", encoding="ascii")


def write_relative_sahara_xml(filename):
    filename.write_text("""<?xml version="1.0" ?>
<sahara_config>
  <chipset>test</chipset>
  <images>
    <image image_id="13" image_path="prog.elf"/>
    <image image_id="38" image_path="cfg.elf"/>
  </images>
</sahara_config>
""", encoding="ascii")


def write_contents_xml(filename):
    filename.write_text("""<?xml version="1.0" ?>
<contents>
  <builds_flat>
    <build>
      <windows_root_path>./</windows_root_path>
      <linux_root_path>./</linux_root_path>
      <file_ref firehose_type="true">
        <file_name>sahara.xml</file_name>
        <file_path>./</file_path>
      </file_ref>
      <download_file>
        <file_name>prog.elf</file_name>
        <file_path>payloads/</file_path>
      </download_file>
      <download_file>
        <file_name>cfg.elf</file_name>
        <file_path>payloads/</file_path>
      </download_file>
      <partition_file storage_type="ufs">
        <file_name>rawprogram.xml</file_name>
        <file_path>./</file_path>
      </partition_file>
    </build>
  </builds_flat>
</contents>
""", encoding="ascii")


def write_multi_contents_xml(filename):
    filename.write_text("""<?xml version="1.0" ?>
<contents>
  <builds_flat>
    <build>
      <windows_root_path>./</windows_root_path>
      <linux_root_path>./</linux_root_path>
      <file_ref firehose_type="true" storage_type="ufs">
        <file_name>ufs-sahara.xml</file_name>
        <file_path>./</file_path>
      </file_ref>
      <file_ref firehose_type="true" storage_type="emmc">
        <file_name>emmc-sahara.xml</file_name>
        <file_path>./</file_path>
      </file_ref>
      <download_file storage_type="ufs">
        <file_name>prog.elf</file_name>
        <file_path>ufs/</file_path>
      </download_file>
      <download_file storage_type="emmc">
        <file_name>prog.elf</file_name>
        <file_path>emmc/</file_path>
      </download_file>
      <partition_file storage_type="ufs">
        <file_name>rawprogram-ufs.xml</file_name>
        <file_path>./</file_path>
      </partition_file>
      <partition_file storage_type="emmc">
        <file_name>rawprogram-emmc.xml</file_name>
        <file_path>./</file_path>
      </partition_file>
    </build>
  </builds_flat>
</contents>
""", encoding="ascii")


def write_single_image_sahara_xml(filename):
    filename.write_text("""<?xml version="1.0" ?>
<sahara_config>
  <chipset>test</chipset>
  <images>
    <image image_id="13" image_path="prog.elf"/>
  </images>
</sahara_config>
""", encoding="ascii")


def assert_archive(filename, expected):
    entries = parse_newc(filename)

    assert entries == expected, \
        f"archive entries mismatch: {sorted(entries)} != {sorted(expected)}"


def assert_no_archive(path):
    assert not path.exists(), f"unexpected archive created: {path}"


def test_mapping(qdl, work, payloads):
    out = work / "mapping.bin"
    payload_dir = work / "payloads"

    run_qdl(qdl, "create-sahara-archive", str(out),
            f"13:{payload_dir / 'prog.elf'},38:{payload_dir / 'cfg.elf'}")

    assert_archive(out, {
        "13:prog.elf": payloads["prog.elf"],
        "38:cfg.elf": payloads["cfg.elf"],
    })


def test_sahara_xml(qdl, work, payloads):
    out = work / "sahara.bin"
    xml = work / "sahara-absolute.xml"

    write_sahara_xml(xml, work / "payloads")
    run_qdl(qdl, "create-sahara-archive", str(out), str(xml))

    assert_archive(out, {
        "13:prog.elf": payloads["prog.elf"],
        "38:cfg.elf": payloads["cfg.elf"],
    })


def test_contents_xml(qdl, work, payloads):
    out = work / "contents.bin"
    xml = work / "contents.xml"

    write_relative_sahara_xml(work / "sahara.xml")
    write_contents_xml(xml)
    run_qdl(qdl, "create-sahara-archive", str(out), str(xml))

    assert_archive(out, {
        "13:prog.elf": payloads["prog.elf"],
        "38:cfg.elf": payloads["cfg.elf"],
    })


def test_contents_xml_selector(qdl, work):
    xml = work / "multi-contents.xml"
    ufs = work / "ufs"
    emmc = work / "emmc"

    ufs.mkdir()
    emmc.mkdir()
    (ufs / "prog.elf").write_bytes(b"ufs programmer")
    (emmc / "prog.elf").write_bytes(b"emmc programmer")
    write_single_image_sahara_xml(work / "ufs-sahara.xml")
    write_single_image_sahara_xml(work / "emmc-sahara.xml")
    write_multi_contents_xml(xml)

    for storage, payload in [("ufs", b"ufs programmer"),
                             ("emmc", b"emmc programmer")]:
        out = work / f"{storage}.bin"
        run_qdl(qdl, "create-sahara-archive", str(out),
                f"{xml}::{storage}")
        assert_archive(out, {"13:prog.elf": payload})

    out = work / "unselected.bin"
    run_qdl(qdl, "create-sahara-archive", str(out), str(xml),
            expect_success=False)
    assert_no_archive(out)


def test_archive_roundtrip(qdl, work, payloads):
    first = work / "roundtrip-source.bin"
    second = work / "roundtrip-result.bin"
    payload_dir = work / "payloads"

    run_qdl(qdl, "create-sahara-archive", str(first),
            f"13:{payload_dir / 'prog.elf'},38:{payload_dir / 'cfg.elf'}")
    run_qdl(qdl, "create-sahara-archive", str(second), str(first))

    assert_archive(second, {
        "13:prog.elf": payloads["prog.elf"],
        "38:cfg.elf": payloads["cfg.elf"],
    })


def test_reject_space_separated(qdl, work):
    out = work / "space-separated.bin"
    payload_dir = work / "payloads"

    run_qdl(qdl, "create-sahara-archive", str(out),
            f"13:{payload_dir / 'prog.elf'}",
            f"38:{payload_dir / 'cfg.elf'}", expect_success=False)
    assert_no_archive(out)


def test_reject_selector_on_non_contents(qdl, work):
    out = work / "selector.bin"
    xml = work / "sahara-selector.xml"

    write_sahara_xml(xml, work / "payloads")
    run_qdl(qdl, "create-sahara-archive", str(out), f"{xml}::ufs",
            expect_success=False)
    assert_no_archive(out)


def test_bad_inputs(qdl, work):
    payload_dir = work / "payloads"
    empty = payload_dir / "empty.bin"
    empty.write_bytes(b"")

    for name, spec in [
        ("invalid-zero-id.bin", f"0:{payload_dir / 'prog.elf'}"),
        ("invalid-large-id.bin", f"128:{payload_dir / 'prog.elf'}"),
        ("missing-payload.bin", f"13:{payload_dir / 'missing.elf'}"),
        ("empty-payload.bin", f"13:{empty}"),
    ]:
        out = work / name
        run_qdl(qdl, "create-sahara-archive", str(out), spec,
                expect_success=False)
        assert_no_archive(out)

    malformed = work / "malformed-sahara.xml"
    malformed.write_text("<?xml version=\"1.0\" ?><not_sahara/>",
                         encoding="ascii")
    out = work / "malformed.bin"
    run_qdl(qdl, "create-sahara-archive", str(out), str(malformed),
            expect_success=False)
    assert_no_archive(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--builddir", required=True)
    args = parser.parse_args()

    qdl = Path(args.builddir) / ("qdl.exe" if os.name == "nt" else "qdl")
    if not qdl.exists():
        raise AssertionError(f"qdl binary not found: {qdl}")

    work = Path(tempfile.mkdtemp(prefix="sahara-archive-",
                                dir=Path(args.builddir) / "tests"))

    try:
        payloads = write_payloads(work / "payloads")

        test_mapping(str(qdl), work, payloads)
        test_sahara_xml(str(qdl), work, payloads)
        test_contents_xml(str(qdl), work, payloads)
        test_contents_xml_selector(str(qdl), work)
        test_archive_roundtrip(str(qdl), work, payloads)
        test_reject_space_separated(str(qdl), work)
        test_reject_selector_on_non_contents(str(qdl), work)
        test_bad_inputs(str(qdl), work)
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
