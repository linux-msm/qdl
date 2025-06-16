#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause

set -e

SCRIPT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

FLAT_BUILD=${SCRIPT_PATH}/data

REP_ROOT=${SCRIPT_PATH}/..
VIP_PATH=${FLAT_BUILD}/vip
EXPECTED_DIGEST="4e13981189ede380172369aa0a4847ca3c9a4e2795733bca90ec1b1e713972ea"
VIP_TABLE_FILE=${VIP_PATH}/DigestsToSign.bin

mkdir -p $VIP_PATH

cd $FLAT_BUILD
${REP_ROOT}/qdl --dry-run --create-digests=${VIP_PATH} \
        prog_firehose_ddr.elf rawprogram*.xml patch*.xml

if command -v sha256sum >/dev/null 2>&1; then
    shacmd="sha256sum"
elif command -v shasum >/dev/null 2>&1; then
    shacmd="shasum -a 256"
else
    echo "No SHA-256 checksum tool found (need 'sha256sum' or 'shasum')"
    exit 1
fi

actual_digest=`${shacmd} "${VIP_TABLE_FILE}" | cut -d ' ' -f1`
if [ "$actual_digest" != "${EXPECTED_DIGEST}" ]; then
	echo "Expected SHA256 digest of ${VIP_TABLE_FILE} file is ${EXPECTED_DIGEST}"
	echo "Calculated SHA256 digest of ${VIP_TABLE_FILE} file is $actual_digest"
	echo "VIP table folder contents:"
	ls -la ${VIP_PATH}
	exit 1
fi

echo "VIP tables are generated successfully and validated"

rm -r ${VIP_PATH}/*.bin
rmdir ${VIP_PATH}
