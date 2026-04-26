#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

ROOT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )/../"

# Run consistently from the repo root regardless of where the script is called
cd "$ROOT_PATH"

# Files to lint. Keep this in sync with .github/workflows/markdown-lint.yml
MARKDOWN_FILES=("README.md")

run_lint() {
	if ! command -v mdl >/dev/null 2>&1; then
		echo "mdl not found." >&2
		echo "Install it with: sudo apt install markdownlint  (or 'gem install mdl')" >&2
		exit 1
	fi
	mdl "${MARKDOWN_FILES[@]}"
}

usage() {
	echo "Usage: $0 {check}"
	exit 2
}

case "${1:-}" in
	check)  run_lint ;;
	*)      usage ;;
esac
