#!/bin/sh

# Make sure running as root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root" 
   exit 1
fi

if [[ -z $1 ]]; then
   echo "Must provide the path to the QFIL image"
   exit 1
fi

QFIL_PATH=$1
if [[ -e ${QFIL_PATH}/prog_firehose_ddr.elf ]]; then
    echo "Found QFIL image in ${QFIL_PATH}"
else
    echo "ERROR- could not find QFIL image in ${QFIL_PATH}"
    exit 1
fi

echo "Flashing QFIL image"

qdl --debug --storage ufs --include ${QFIL_PATH} ${QFIL_PATH}/prog_firehose_ddr.elf ${QFIL_PATH}/rawprogram*.xml ${QFIL_PATH}/patch*.xml

