#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause

set -e

SCRIPT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --builddir)
            builddir="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

if [[ -z "${builddir}" ]]; then
    echo "Error: --builddir is required." >&2
    exit 1
fi

FLAT_BUILD=${SCRIPT_PATH}/data
QDL_PATH=$builddir

uname_out="$(uname -s)"
case "${uname_out}" in
    Linux*|Darwin*)
        QDL=qdl
        ;;
    CYGWIN*|MINGW*|MSYS*)
        QDL=qdl.exe
        ;;
    *)
        exit 1
        ;;
esac

DATA_SRC=${SCRIPT_PATH}/data
FLAT_BUILD=${builddir}/tests/data

${DATA_SRC}/generate_flat_build.sh "${FLAT_BUILD}" >&2

cd $FLAT_BUILD

echo "1..2"

n=1
fail=0

run_test() {
	desc="$1"
	shift
	out="$(mktemp)"
	err="$(mktemp)"

	if "$@" >"$out" 2>"$err" ; then
		echo "ok $n - $desc"
	else
		rc=$?
		echo "not ok $n - $desc"
		echo "# exit code: $rc"

		sed 's/^/# stdout: /' "$out"
		sed 's/^/# stderr: /' "$err"

		fail=1
	fi
	n=$((n + 1))
}

#echo "####### Flashing flashmap.json"
run_test "flashing flashmap.json" ${QDL_PATH}/${QDL} --dry-run flash flashmap.json

#echo "####### Flashing flashmap.zip"
run_test "flashing flashmap.zip" ${QDL_PATH}/${QDL} --dry-run flash flashmap.zip

exit $fail
