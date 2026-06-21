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
#                       erase/sha256/reboot steps). Defaults to the first
#                       prog_firehose_*.elf next to QDL_HIL_BUILD.
#   QDL_HIL_PARTITION   Partition for the read/erase/write steps. The EFI
#                       system partition is small and quick to read and write,
#                       so it defaults to "efi".
#   QDL_HIL_IMAGE       Image the erase/write steps restore. Defaults to the
#                       data the read step reads back, so erase/write
#                       round-trip the partition's own content.
#   QDL_HIL_SERIAL      -S serial, to select one of several attached devices.
#
# Every step runs with --skip-reset to keep the programmer alive across
# invocations; only the final reboot step omits it to reset the board.

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

if [[ "${step}" == "flash_full" ]]; then
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

# Fail-fast: if an earlier step already failed, stop the whole suite.
if [[ "${step}" != "flash_full" && -e "${ABORT_FILE}" ]]; then
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

# Common qdl prefix. -R suppresses the reset everywhere but the final reboot.
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

# Final step: reboot the board (the only command without --skip-reset).
t_reboot() {
    need_programmer
    local out; out="$(mktemp -p "${HIL_TMPDIR}")"
    trap 'rm -f "${out}"' RETURN
    qdl_reset "${PROGRAMMER}" read 0/0+1 "${out}"	# 1-sector read, then reset
    rm -f "${SAVED_IMAGE}"	# done; drop the readback backup
}

# --- dispatch -------------------------------------------------------------
case "${step}" in
    flash_full)         t_flash_full ;;
    read_partition)     t_read_partition ;;
    read_gpt)           t_read_gpt ;;
    read_addr_sectors)  t_read_addr_sectors ;;
    read_addr_partname) t_read_addr_partname ;;
    flash_skipblock)    t_flash_skipblock ;;
    sha256)             t_sha256 ;;
    erase)              t_erase ;;
    write_image)        t_write_image ;;
    reboot)             t_reboot ;;
    *) echo "unknown step: ${step}" >&2; exit 99 ;;
esac
