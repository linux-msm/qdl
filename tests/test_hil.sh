#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
#
# Hardware-in-the-loop (HIL) integration test steps.
#
# Each step is registered as its own meson test in the "hil" suite (see
# tests/meson.build) and invoked as:
#
#   test_hil.sh --builddir <dir> --step <name>
#
# The steps drive the built qdl binary against a real device in EDL mode, so
# they are not meant for unattended CI. Run them explicitly with:
#
#   QDL_HIL_BUILD=/path/to/build QDL_HIL_STORAGE=ufs \
#       meson test -C build --suite hil
#
# When the required environment is not set a step exits 77, which meson
# reports as SKIP, so a plain "meson test" stays green without hardware.
#
# Required environment:
#   QDL_HIL_BUILD     Build to flash. Auto-detected: a directory uses classic
#                     flashing (prog_firehose_*.elf + rawprogram*.xml +
#                     patch*.xml); a file (.json/.xml/.zip) uses "qdl flash".
#   QDL_HIL_STORAGE   Target storage type: emmc|nand|nvme|spinor|ufs.
#
# Optional environment:
#   QDL_HIL_PROGRAMMER  Firehose programmer .elf (needed by the read/write/
#                       erase/sha256/reset steps). Defaults to the first
#                       prog_firehose_*.elf next to QDL_HIL_BUILD.
#   QDL_HIL_PARTITION   Partition for the read/erase/write steps. The EFI
#                       system partition is small and quick to read and write,
#                       so it defaults to "efi".
#   QDL_HIL_IMAGE       Image the erase/write steps restore. Defaults to the
#                       data the read step reads back, so erase/write
#                       round-trip the partition's own content.
#   QDL_HIL_SERIAL      -S serial, to select one of several attached devices.
#   QDL_HIL_CONTENTS    contents.xml (optionally with ::specifier) for the
#                       create-zip step; when unset that step is skipped.
#   QDL_HIL_SPARSE      flashmap/contents/zip that flashes sparse images, for
#                       the sparse-flash step; when unset that step is skipped.
#
# Every step runs with --skip-reset to keep the programmer alive across
# invocations; only the final step ends the session with the reset verb.

set -e

SKIP=77

while [[ $# -gt 0 ]]; do
    case "$1" in
        --builddir) builddir="$2"; shift 2 ;;
        --step)     step="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

[[ -n "${builddir}" ]] || { echo "Error: --builddir is required." >&2; exit 1; }
[[ -n "${step}" ]]     || { echo "Error: --step is required." >&2; exit 1; }

uname_out="$(uname -s)"
case "${uname_out}" in
    Linux*|Darwin*) QDL=qdl ;;
    CYGWIN*|MINGW*|MSYS*) QDL=qdl.exe ;;
    *) exit 1 ;;
esac
QEXE="${builddir}/${QDL}"

shopt -s nullglob

# --- fail-fast across the (serialized) suite ------------------------------
# meson runs each step as its own process, so persist a sentinel: the first
# step starts a fresh run, any real failure raises it, and every later step
# bails out instead of touching the device once it is set. A skipped step
# (exit 77) does not count as a failure.
ABORT_FILE="${builddir}/meson-logs/hil.aborted"
# Keep all HIL scratch (partition readbacks, temporary reads) on the build
# disk, never in /tmp - which is commonly a small tmpfs that a full-partition
# readback would exhaust.
HIL_TMPDIR="${builddir}/hil-tmp"
mkdir -p "${HIL_TMPDIR}"
# Readback of the target partition captured by the read step and written back
# after the erase step, so erase/write restore the exact original bytes.
SAVED_IMAGE="${HIL_TMPDIR}/readback.img"

on_exit() {
    local rc=$?
    if [[ ${rc} -ne 0 && ${rc} -ne ${SKIP} ]]; then
        mkdir -p "$(dirname "${ABORT_FILE}")" 2>/dev/null || true
        : > "${ABORT_FILE}"
    fi
}
trap on_exit EXIT

# The suite's first step (highest meson priority). Suite-level setup -
# resetting the fail-fast sentinel, entering EDL mode - must anchor to it:
# doing either in a later step breaks the steps running before it. Keep
# this in sync with the priorities in tests/meson.build.
FIRST_STEP="list"

if [[ "${step}" == "${FIRST_STEP}" ]]; then
    rm -f "${ABORT_FILE}"
    rm -rf "${HIL_TMPDIR:?}"/*
fi

# --- resolve configuration from the environment ---------------------------
BUILD="${QDL_HIL_BUILD}"
STORAGE="${QDL_HIL_STORAGE}"
PARTITION="${QDL_HIL_PARTITION:-efi}"
SERIAL="${QDL_HIL_SERIAL}"
PROGRAMMER="${QDL_HIL_PROGRAMMER}"
IMAGE="${QDL_HIL_IMAGE}"

if [[ -z "${BUILD}" || -z "${STORAGE}" ]]; then
    echo "set QDL_HIL_BUILD and QDL_HIL_STORAGE to run the HIL suite" >&2
    exit ${SKIP}
fi

# Fail-fast: if an earlier step already failed, stop the whole suite. No
# step is exempt - the first step cleared the sentinel above before this
# check, so a set sentinel always means a failure in this run.
if [[ -e "${ABORT_FILE}" ]]; then
    echo "an earlier HIL step failed; stopping the suite" >&2
    exit ${SKIP}
fi

IS_DIR=false
if [[ -d "${BUILD}" ]]; then
    IS_DIR=true
    BUILDDIR="${BUILD}"
else
    BUILDDIR="$(dirname "${BUILD}")"
fi

if [[ -z "${PROGRAMMER}" ]]; then
    progs=( "${BUILDDIR}"/prog_firehose_*.elf )
    [[ ${#progs[@]} -gt 0 ]] && PROGRAMMER="${progs[0]}"
fi

# Image the erase/write steps restore: an explicit QDL_HIL_IMAGE if given,
# otherwise the readback captured by the read step.
IMAGE="${IMAGE:-${SAVED_IMAGE}}"

# Common qdl prefix. -R suppresses the reset everywhere but the final step.
COMMON=( -s "${STORAGE}" )
[[ -n "${SERIAL}" ]] && COMMON+=( -S "${SERIAL}" )

qdl_noreset() { "${QEXE}" "${COMMON[@]}" -R "$@"; }
qdl_reset()   { "${QEXE}" "${COMMON[@]}" "$@"; }

need_programmer() {
    [[ -n "${PROGRAMMER}" ]] || {
        echo "no prog_firehose_*.elf found (set QDL_HIL_PROGRAMMER)" >&2
        exit ${SKIP}
    }
}

# Print the hex sha256 of a file, using whichever tool is available.
sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        echo "no sha256 tool (sha256sum/shasum) available" >&2
        exit ${SKIP}
    fi
}

# --- step implementations -------------------------------------------------

# a) Flash the whole build.
t_flash_full() {
    if ${IS_DIR}; then
        need_programmer
        local raws=( "${BUILDDIR}"/rawprogram*.xml )
        local patches=( "${BUILDDIR}"/patch*.xml )
        [[ ${#raws[@]} -gt 0 ]] || { echo "no rawprogram*.xml in ${BUILDDIR}" >&2; exit ${SKIP}; }
        qdl_noreset "${PROGRAMMER}" "${raws[@]}" "${patches[@]}"
    else
        qdl_noreset flash "${BUILD}"
    fi
}

# b) Read a partition by name (EFI by default). The readback is kept so the
# write step can restore it after erase.
t_read_partition() {
    need_programmer
    qdl_noreset "${PROGRAMMER}" read "${PARTITION}" "${SAVED_IMAGE}"
    [[ -s "${SAVED_IMAGE}" ]] || { echo "read produced an empty file" >&2; return 1; }
}

# c) Read only the GPT and confirm it really is one.
t_read_gpt() {
    need_programmer
    local out; out="$(mktemp -p "${HIL_TMPDIR}")"
    trap 'rm -f "${out}"' RETURN
    qdl_noreset "${PROGRAMMER}" read 0/0+34 "${out}"
    grep -qa "EFI PART" "${out}" || { echo "no GPT signature in readback" >&2; return 1; }
}

# c') Other supported read address forms.
t_read_addr_sectors() {	# P/S+L
    need_programmer
    local out; out="$(mktemp -p "${HIL_TMPDIR}")"
    trap 'rm -f "${out}"' RETURN
    qdl_noreset "${PROGRAMMER}" read 0/0+1 "${out}"
    [[ -s "${out}" ]] || { echo "empty readback" >&2; return 1; }
}

t_read_addr_partname() {	# P/name
    need_programmer
    local out; out="$(mktemp -p "${HIL_TMPDIR}")"
    trap 'rm -f "${out}"' RETURN
    qdl_noreset "${PROGRAMMER}" read "0/${PARTITION}" "${out}"
    [[ -s "${out}" ]] || { echo "empty readback" >&2; return 1; }
}

# d) Re-flash the build with --skipblock=sha256 (should mostly skip).
t_flash_skipblock() {
    if ${IS_DIR}; then
        need_programmer
        local raws=( "${BUILDDIR}"/rawprogram*.xml )
        local patches=( "${BUILDDIR}"/patch*.xml )
        [[ ${#raws[@]} -gt 0 ]] || { echo "no rawprogram*.xml" >&2; exit ${SKIP}; }
        qdl_noreset --skipblock=sha256 "${PROGRAMMER}" "${raws[@]}" "${patches[@]}"
    else
        qdl_noreset --skipblock=sha256 flash "${BUILD}"
    fi
}

# f) sha256 digest command.
t_sha256() {
    need_programmer
    local out; out="$(mktemp -p "${HIL_TMPDIR}")"
    trap 'rm -f "${out}"' RETURN
    qdl_noreset "${PROGRAMMER}" sha256 0/0+34 >"${out}" 2>&1
    grep -qiE '[0-9a-f]{64}' "${out}" || { echo "no sha256 digest in output:" >&2; cat "${out}" >&2; return 1; }
}

# g) Erase the partition. Only erase when there is an image to restore it with
# (the readback from the read step, or an explicit QDL_HIL_IMAGE), so the suite
# never leaves the partition wiped.
t_erase() {
    need_programmer
    [[ -f "${IMAGE}" ]] || { echo "no image to restore (run the read step or set QDL_HIL_IMAGE)" >&2; exit ${SKIP}; }
    qdl_noreset "${PROGRAMMER}" erase "${PARTITION}"
}

# f') Write the image back to its partition (the readback by default).
t_write_image() {
    need_programmer
    [[ -f "${IMAGE}" ]] || { echo "no image to write (run the read step or set QDL_HIL_IMAGE)" >&2; exit ${SKIP}; }
    qdl_noreset "${PROGRAMMER}" write "${PARTITION}" "${IMAGE}"
}

# Final step: reset the board with the explicit reset verb, ending the
# Firehose session that every other step kept alive with --skip-reset.
t_reset() {
    need_programmer
    qdl_reset "${PROGRAMMER}" reset
    rm -f "${SAVED_IMAGE}"	# done; drop the readback backup
}

# h) Enumerate EDL devices; the attached device must appear.
t_list() {
    local out; out="$(mktemp -p "${HIL_TMPDIR}")"
    trap 'rm -f "${out}"' RETURN
    "${QEXE}" list >"${out}" 2>&1 || { echo "qdl list failed" >&2; cat "${out}" >&2; return 1; }
    if grep -qi "No devices found" "${out}" ||
       ! grep -qiE '^[0-9a-f]{4}:[0-9a-f]{4}' "${out}"; then
        echo "qdl list did not report an attached device:" >&2
        cat "${out}" >&2
        return 1
    fi
}

# i) Package a contents.xml into a zip with create-zip, then flash the zip
# (the programmer comes from inside the zip).
t_create_zip() {
    [[ -n "${QDL_HIL_CONTENTS}" ]] || {
        echo "set QDL_HIL_CONTENTS to test create-zip" >&2
        exit ${SKIP}
    }
    local zip="${HIL_TMPDIR}/hil-created.zip"
    trap 'rm -f "${zip}"' RETURN
    "${QEXE}" create-zip "${zip}" "${QDL_HIL_CONTENTS}" ||
        { echo "create-zip failed" >&2; return 1; }
    [[ -s "${zip}" ]] || { echo "create-zip produced no output" >&2; return 1; }
    qdl_noreset flash "${zip}"
}

# j) Flash a build that uses sparse (Android sparse format) images.
t_sparse_flash() {
    [[ -n "${QDL_HIL_SPARSE}" && -f "${QDL_HIL_SPARSE}" ]] || {
        echo "set QDL_HIL_SPARSE to a flashmap/contents/zip using sparse images" >&2
        exit ${SKIP}
    }
    qdl_noreset flash "${QDL_HIL_SPARSE}"
}

# k) Error handling: a read of a nonexistent physical partition must be NAKed
# by the device and reported as a failure, not silently succeed.
t_nak_read() {
    need_programmer
    local out; out="$(mktemp -p "${HIL_TMPDIR}")"
    trap 'rm -f "${out}"' RETURN
    if qdl_noreset "${PROGRAMMER}" read 99/0+1 "${out}" >/dev/null 2>&1; then
        echo "reading nonexistent physical partition 99 unexpectedly succeeded" >&2
        return 1
    fi
}

# l) sha256 digest of a partition addressed by GPT name.
t_sha256_name() {
    need_programmer
    local out; out="$(mktemp -p "${HIL_TMPDIR}")"
    trap 'rm -f "${out}"' RETURN
    qdl_noreset "${PROGRAMMER}" sha256 "${PARTITION}" >"${out}" 2>&1
    grep -qiE '[0-9a-f]{64}' "${out}" ||
        { echo "no sha256 digest for ${PARTITION}:" >&2; cat "${out}" >&2; return 1; }
}

# m) Re-write the image with an overridden transfer chunk size.
t_write_chunked() {
    need_programmer
    [[ -f "${IMAGE}" ]] || {
        echo "no image to write (run the read step or set QDL_HIL_IMAGE)" >&2
        exit ${SKIP}
    }
    "${QEXE}" "${COMMON[@]}" -R --out-chunk-size=16384 \
        "${PROGRAMMER}" write "${PARTITION}" "${IMAGE}"
}

# n) Integrity check: the device's digest of the partition must match a local
# sha256 of the bytes we read back (and just wrote), proving the erase/write
# round-trip preserved the content.
t_verify_write() {
    need_programmer
    [[ -f "${SAVED_IMAGE}" ]] || {
        echo "no readback to verify against (the read step did not run)" >&2
        exit ${SKIP}
    }
    local out; out="$(mktemp -p "${HIL_TMPDIR}")"
    trap 'rm -f "${out}"' RETURN
    qdl_noreset "${PROGRAMMER}" sha256 "${PARTITION}" >"${out}" 2>&1 ||
        { echo "sha256 command failed" >&2; cat "${out}" >&2; return 1; }
    local dev_digest local_digest
    dev_digest="$(grep -oiE '[0-9a-f]{64}' "${out}" | head -1)"
    [[ -n "${dev_digest}" ]] || { echo "no device digest:" >&2; cat "${out}" >&2; return 1; }
    local_digest="$(sha256_file "${SAVED_IMAGE}")"
    [[ "${dev_digest}" == "${local_digest}" ]] || {
        echo "partition digest mismatch: device=${dev_digest} local=${local_digest}" >&2
        return 1
    }
}

# o) Chip identity through Sahara command mode. Must run before the
# programmer upload: chipinfo is only available while the device still
# speaks Sahara. The verb switches the device back to image-transfer mode
# on exit, so the following flash step still gets its Sahara HELLO.
t_chipinfo() {
    local out; out="$(mktemp -p "${HIL_TMPDIR}")"
    trap 'rm -f "${out}"' RETURN
    local args=()
    [[ -n "${SERIAL}" ]] && args+=( -S "${SERIAL}" )
    if ! "${QEXE}" "${args[@]}" chipinfo >"${out}" 2>&1; then
        if grep -qi "already in Firehose mode" "${out}"; then
            echo "programmer already running; chipinfo needs a fresh Sahara device" >&2
            exit ${SKIP}
        fi
        echo "chipinfo failed:" >&2
        cat "${out}" >&2
        return 1
    fi
    grep -qiE 'chip serial number|hw id' "${out}" ||
        { echo "chipinfo reported no chip identity:" >&2; cat "${out}" >&2; return 1; }
}

# --- dispatch -------------------------------------------------------------
case "${step}" in
    list)               t_list ;;
    chipinfo)           t_chipinfo ;;
    flash_full)         t_flash_full ;;
    create_zip)         t_create_zip ;;
    sparse_flash)       t_sparse_flash ;;
    read_partition)     t_read_partition ;;
    read_gpt)           t_read_gpt ;;
    read_addr_sectors)  t_read_addr_sectors ;;
    read_addr_partname) t_read_addr_partname ;;
    nak_read)           t_nak_read ;;
    flash_skipblock)    t_flash_skipblock ;;
    sha256)             t_sha256 ;;
    sha256_name)        t_sha256_name ;;
    erase)              t_erase ;;
    write_image)        t_write_image ;;
    write_chunked)      t_write_chunked ;;
    verify_write)       t_verify_write ;;
    reset)              t_reset ;;
    *) echo "unknown step: ${step}" >&2; exit 99 ;;
esac
