#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

ROOT_URL="https://raw.githubusercontent.com/torvalds/linux/v6.15/scripts"
CHECKPATCH_URL="${ROOT_URL}/checkpatch.pl"
SPELLING_URL="${ROOT_URL}/spelling.txt"

ROOT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )/../"
CHECKPATCH_DIR="${ROOT_PATH}/.scripts"
CHECKPATCH="${CHECKPATCH_DIR}/checkpatch.pl"
SPELLING="${CHECKPATCH_DIR}/spelling.txt"

# Just to repo root to work consistently from anywhere
cd "$ROOT_PATH"

# List only tracked source files (not ignored/untracked)
CHECKPATCH_SOURCES=$(git ls-files | grep -E '\.(c|h|sh)$' | \
					grep -vE '(^|/)sha2\.(c|h)$' | \
					grep -vE 'version\.h$' | \
					grep -vE 'list\.h$'
)

ensure_checkpatch() {
	if [[ ! -x "$CHECKPATCH" ]]; then
		echo "Downloading checkpatch.pl and spelling.txt..."
		mkdir -p "$CHECKPATCH_DIR"
		curl -sSfL "$CHECKPATCH_URL" -o "$CHECKPATCH"
		curl -sSfL "$SPELLING_URL" -o "$SPELLING"
		chmod +x "$CHECKPATCH"
	fi
}

do_check_all() {
	ensure_checkpatch
	echo "Running checkpatch on tracked source files (excluding sha2.c, sha2.h)..."
	for file in ${CHECKPATCH_SOURCES}; do
		perl "${CHECKPATCH}" --no-tree -f "$file" || exit 1
	done
}

do_check_cached() {
	ensure_checkpatch
	echo "Running checkpatch on staged changes..."
	git diff --cached -- . | perl "$CHECKPATCH" --no-tree -
}

usage() {
	echo "Usage: $0 {download-checkpatch|check|check-cached}"
	exit 2
}

case "${1:-}" in
	download)      ensure_checkpatch ;;
	check)         do_check_all ;;
	check-cached)  do_check_cached ;;
	*)             usage ;;
esac
