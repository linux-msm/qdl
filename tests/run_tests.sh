#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause

set -e

SCRIPT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
FLAT_BUILD_PATH=$SCRIPT_PATH/data

echo "####### Generate a FLAT build"
$FLAT_BUILD_PATH/generate_flat_build.sh

echo "####### Run QDL tests"
cd $SCRIPT_PATH
for t in test_*.sh; do
	echo "###### Run $t"
	bash $t
	if [ $? -eq 0 ]; then
		echo "####### Test $t: OK"
	else
		echo "####### Test $t: FAIL"
		failed=1
	fi
done

echo "####### Housekeeping"
rm -f ${FLAT_BUILD_PATH}/*.bin ${FLAT_BUILD_PATH}/*.img
rm -f ${FLAT_BUILD_PATH}/*.elf

if [ "$failed" == "1" ]; then
	echo "####### Some test failed"
	exit 1
fi

echo "####### All tests passed"
