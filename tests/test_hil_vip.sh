#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
#
# VIP-mode HIL step: sign the firehose programmer, generate and sign the
# VIP digest tables, then flash the build in VIP mode on a secured board.
#
# Registered as the final step of the "hil-vip" meson suite; the list and
# chipinfo steps of that suite are reused from test_hil.sh, so the usual
# QDL_HIL_BUILD/QDL_HIL_STORAGE (+ QDL_HIL_SERIAL/QDL_HIL_ENTER_EDL_CMD
# for the rig) apply. On top of those, this step requires:
#
#   QDL_HIL_SECTOOLS    sectools v2 binary
#   QDL_HIL_SECPROFILE  *_security_profile.xml for the target
#   QDL_HIL_SECKEYS     OEM keys directory from genkeys.sh
#                       (qpsa_rootca0.cer, qpsa_attestca0.cer/.key)
#
# When any of them is missing the step exits 77 (meson SKIP), so suites
# stay green on rigs without signing material.
#
# Only the programmer and the digest-table mbn are signed: VIP validates
# the flashed data stream against the signed tables, so the individual
# images need no signatures for this test. The signing recipe mirrors
# signimages.sh from the qcom-sec-tools repository, with the image ids
# taken from its mapping (DEVICE-PROGRAMMER, VIP) and key index 0.

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
[[ "${step}" == "sign_flash" ]] || { echo "Unknown step: ${step}" >&2; exit 1; }

uname_out="$(uname -s)"
case "${uname_out}" in
    Linux*|Darwin*) QDL=qdl ;;
    CYGWIN*|MINGW*|MSYS*) QDL=qdl.exe ;;
    *) exit 1 ;;
esac
QEXE="${builddir}/${QDL}"

shopt -s nullglob

# Shared with test_hil.sh: honor the suite-wide fail-fast sentinel and
# raise it if this step fails (see the fail-fast block there).
ABORT_FILE="${builddir}/meson-logs/hil.aborted"

on_exit() {
    local rc=$?
    if [[ ${rc} -ne 0 && ${rc} -ne ${SKIP} ]]; then
        mkdir -p "$(dirname "${ABORT_FILE}")" 2>/dev/null || true
        : > "${ABORT_FILE}"
    fi
}
trap on_exit EXIT

BUILD="${QDL_HIL_BUILD}"
STORAGE="${QDL_HIL_STORAGE}"
SERIAL="${QDL_HIL_SERIAL}"
SECTOOLS="${QDL_HIL_SECTOOLS}"
SECPROFILE="${QDL_HIL_SECPROFILE}"
SECKEYS="${QDL_HIL_SECKEYS}"

if [[ -z "${BUILD}" || -z "${STORAGE}" ]]; then
    echo "set QDL_HIL_BUILD and QDL_HIL_STORAGE to run the HIL suites" >&2
    exit ${SKIP}
fi

if [[ -z "${SECTOOLS}" || -z "${SECPROFILE}" || -z "${SECKEYS}" ]]; then
    echo "set QDL_HIL_SECTOOLS, QDL_HIL_SECPROFILE and QDL_HIL_SECKEYS" \
         "to run the hil-vip suite" >&2
    exit ${SKIP}
fi

if [[ -e "${ABORT_FILE}" ]]; then
    echo "an earlier HIL step failed; stopping the suite" >&2
    exit ${SKIP}
fi

[[ -x "${SECTOOLS}" ]] || { echo "sectools not executable: ${SECTOOLS}" >&2; exit ${SKIP}; }
[[ -f "${SECPROFILE}" ]] || { echo "security profile not found: ${SECPROFILE}" >&2; exit ${SKIP}; }

# VIP flashing follows the classic rawprogram flow, so a flat build
# directory is required (not a flashmap/zip).
if [[ ! -d "${BUILD}" ]]; then
    echo "hil-vip requires a flat build directory in QDL_HIL_BUILD" >&2
    exit ${SKIP}
fi

progs=( "${BUILD}"/prog_firehose_*.elf )
raws=( "${BUILD}"/rawprogram*.xml )
patches=( "${BUILD}"/patch*.xml )
[[ ${#progs[@]} -gt 0 ]] || { echo "no prog_firehose_*.elf in ${BUILD}" >&2; exit ${SKIP}; }
[[ ${#raws[@]} -gt 0 ]]  || { echo "no rawprogram*.xml in ${BUILD}" >&2; exit ${SKIP}; }

COMMON=( -s "${STORAGE}" )
[[ -n "${SERIAL}" ]] && COMMON+=( -S "${SERIAL}" )

# Fresh workspace on the build disk; the pristine build artifacts are
# never modified - the programmer is signed on a copy.
WORK="${builddir}/hil-vip"
TABLES="${WORK}/vip-tables"
rm -rf "${WORK:?}"
mkdir -p "${TABLES}"

PROG="${WORK}/$(basename "${progs[0]}")"
cp "${progs[0]}" "${PROG}"

# Sign one image in place, following the signimages.sh recipe with
# signing key index 0.
sign_one() {
    local file="$1" image_id="$2"
    local root_cert="${SECKEYS}/qpsa_rootca0.cer"
    local ca_cert="${SECKEYS}/qpsa_attestca0.cer"
    local ca_key="${SECKEYS}/qpsa_attestca0.key"

    if [[ ! -f "${root_cert}" || ! -f "${ca_cert}" || ! -f "${ca_key}" ]]; then
        echo "OEM keys not found under ${SECKEYS} (expected qpsa_rootca0.cer," \
             "qpsa_attestca0.cer, qpsa_attestca0.key)" >&2
        exit ${SKIP}
    fi

    "${SECTOOLS}" secure-image \
        --sign "${file}" --image-id="${image_id}" \
        --security-profile "${SECPROFILE}" \
        --anti-rollback-version=0x0 \
        --signing-mode LOCAL \
        --root-certificate "${root_cert}" \
        --ca-certificate="${ca_cert}" --ca-key="${ca_key}" \
        --outfile "${file}" \
        || { echo "signing ${file} as ${image_id} failed" >&2; return 1; }
}

echo "=== signing programmer $(basename "${PROG}")"
sign_one "${PROG}" DEVICE-PROGRAMMER

echo "=== generating VIP digest tables"
"${QEXE}" --allow-fusing --create-digests "${TABLES}" \
          "${PROG}" "${raws[@]}" "${patches[@]}"
[[ -f "${TABLES}/DigestsToSign.bin" ]] || {
    echo "digest generation produced no DigestsToSign.bin" >&2
    exit 1
}

echo "=== wrapping digest table into mbn"
"${SECTOOLS}" mbn-tool generate --data "${TABLES}/DigestsToSign.bin" \
              --mbn-version 6 --outfile "${TABLES}/DigestsToSign.bin.mbn"

echo "=== signing digest table mbn"
sign_one "${TABLES}/DigestsToSign.bin.mbn" VIP

echo "=== flashing in VIP mode"
"${QEXE}" "${COMMON[@]}" --allow-fusing --vip-table-path "${TABLES}" \
          "${PROG}" "${raws[@]}" "${patches[@]}"
