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

do_check_range() {
	ensure_checkpatch

	local base="${CHECKPATCH_BASE:-origin/master}"
	local head="${CHECKPATCH_HEAD:-HEAD}"

	if ! git rev-parse --verify "${base}" >/dev/null 2>&1; then
		echo "Base ref '${base}' is not available." >&2
		echo "Set CHECKPATCH_BASE to a fetched ref (e.g. origin/master)." >&2
		exit 1
	fi

	local commits
	commits=$(git rev-list --reverse "${base}..${head}")
	if [[ -z "${commits}" ]]; then
		echo "No commits in range ${base}..${head}; nothing to check."
		return 0
	fi

	echo "Running checkpatch on commits in range ${base}..${head}..."
	local fail=0
	for commit in ${commits}; do
		echo
		echo "--- Checking commit ${commit} ---"
		if ! git format-patch -1 --stdout "${commit}" | perl "${CHECKPATCH}" --no-tree -; then
			fail=1
		fi
	done
	return $fail
}

usage() {
	echo "Usage: $0 {download-checkpatch|check|check-cached|check-range}"
	echo
	echo "  check-range   Run checkpatch per-commit on the range"
	echo "                \$CHECKPATCH_BASE..\$CHECKPATCH_HEAD"
	echo "                (defaults: origin/master..HEAD)"
	exit 2
}

case "${1:-}" in
	download)      ensure_checkpatch ;;
	check)         do_check_all ;;
	check-cached)  do_check_cached ;;
	check-range)   do_check_range ;;
	*)             usage ;;
esac
