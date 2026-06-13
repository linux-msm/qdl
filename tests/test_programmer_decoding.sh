#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause

SCRIPT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
FLAT_BUILD=${SCRIPT_PATH}/data
REP_ROOT=${SCRIPT_PATH}/..
QDL=${REP_ROOT}/qdl

ANY_FAIL=0

cd $FLAT_BUILD

die() { echo "FAIL: $*" >&2; exit 1; }
need_cmd() { command -v "$1" >/dev/null 2>&1 || die "Missing required command: $1"; }

between_markers() {
  local haystack="$1" start="$2" end="$3"
  awk -v s="$start" -v e="$end" '
    $0 ~ s {inside=1; next}
    $0 ~ e {inside=0}
    inside {print}
  ' <<<"$haystack" | sed -e 's/[[:space:]]\+$//' -e '/^[[:space:]]*$/d'
}

assert_programmer() {
    local programmer="$1"; shift
    local expected_exit_code=$1; shift
    local expected="$(printf '%s\n' "$@")"

    out="$(${QDL} --dry-run --debug "$programmer" rawprogram0.xml 2>&1)"
    if (( $? != expected_exit_code )); then
        printf "%-60s FAIL\n" "$programmer"
        echo "Got:"
        echo "$out"
        ANY_FAIL=1
        return
    fi

    block="$(between_markers "$out" "Sahara images:" "waiting for")"

    if [[ "$block" == "$expected" ]]; then
        printf "%-60s OK\n" "$programmer"
    else
        printf "%-60s FAIL\n" "$programmer"
        echo "Expected:"
        echo "$expected"
        echo "Got:"
        #echo "$out"
        echo "$block"

        ANY_FAIL=1
    fi
}

mk_fixture_sahara() {
  local out="$1"; shift
  local pair id path

  {
    echo '<?xml version="1.0" ?>'
    echo '<sahara_config>'
    echo $'\t<images>'

    for pair in "$@"; do
      id="${pair%%:*}"
      path="${pair#*:}"
      printf $'\t\t<image image_id="%s" image_path="%s" />\n' "$id" "$path"
    done

    echo $'\t</images>'
    echo '</sahara_config>'
  } > "$out"
}

mk_fixture_cpio() {
  local out="$1"; shift
  local pair id file tmpdir

  tmpdir="$(mktemp -d)"
  trap 'rm -rf "$tmpdir"' RETURN

  for pair in "$@"; do
    id="${pair%%:*}"
    file="${pair#*:}"

    # Create a symlink with the desired name inside a temp dir
    ln -s "../$file" "$tmpdir/$id:$file" 2> /dev/null || {
        echo "Unable to create cpio archive: can't create '$id:$file'"
        return
    }
  done

  (
    cd "$tmpdir"
    printf '%s\n' * | cpio -o -H newc 2> /dev/null
  ) > "$out"
}

need_cmd awk
need_cmd sed
need_cmd cpio

#
# Create test content
#
cp prog_firehose_ddr.elf prog_A.elf
cp prog_firehose_ddr.elf prog_B.elf
cp prog_firehose_ddr.elf prog_C.elf
cp prog_firehose_ddr.elf c:\\prog_D.elf
cp prog_firehose_ddr.elf 13prog_A.elf
cp prog_firehose_ddr.elf 13a:14prog_A.elf || true

mk_fixture_sahara "sahara-prog_A-prog_B.xml" "13:prog_A.elf" "14:prog_B.elf"
mk_fixture_sahara "sahara-invalid-id.xml" "999:prog_A.elf"
mk_fixture_sahara "sahara-windows.xml" "13:c:\\prog_D.elf"
mk_fixture_sahara "sahara-non-exist.xml" "999:nonexisting-programmer.elf"
mk_fixture_sahara "sahara-empty-path.xml" "13:"
mk_fixture_sahara "sahara-absolute-path.xml" "13:$(readlink -f prog_A.elf)"

mk_fixture_cpio "sahara-prog_A-prog_B.cpio"  "13:prog_A.elf" "14:prog_B.elf"
mk_fixture_cpio "sahara-invalid-id.cpio"  "999:prog_A.elf"

#
# Test single file, no ids
#
assert_programmer "prog_A.elf" 0 "  13: prog_A.elf"

assert_programmer "c:\\prog_D.elf" 0 "  13: c:\\prog_D.elf"

#
# Test programmers specified with ID on command line
#
assert_programmer "13:c:\\prog_D.elf" 0 "  13: c:\\prog_D.elf"

assert_programmer "13:prog_A.elf" 0 "  13: prog_A.elf"

assert_programmer "13:prog_A.elf,14:prog_B.elf" 0 \
    "  13: prog_A.elf" \
    "  14: prog_B.elf"

assert_programmer "13:prog_A.elf,14:prog_B.elf,15:prog_C.elf" 0 \
    "  13: prog_A.elf" \
    "  14: prog_B.elf" \
    "  15: prog_C.elf"

assert_programmer "13prog_A.elf" 0 "  13: 13prog_A.elf"

if [ -z "$MSYSTEM" ]; then
    assert_programmer "13a:14prog_A.elf" 0 "  13: 13a:14prog_A.elf"
else
    printf "%-60s SKIP\n" "13a:14prog_A.elf"
fi

#
# Test Sahara device programmer XML handling
#
assert_programmer "sahara-prog_A-prog_B.xml" 0 \
    "  13: ./prog_A.elf" \
    "  14: ./prog_B.elf"

if [ -n "$MSYSTEM" ]; then
assert_programmer "sahara-windows.xml" 0 \
    "  13: c:\\prog_D.elf"
else
printf "%-60s SKIP\n" "sahara-windows.xml"
fi

assert_programmer "sahara-invalid-id.xml" 1 ""
assert_programmer "sahara-non-exist.xml" 1 ""
assert_programmer "sahara-empty-path.xml" 1 ""

#
# Test Sahara Archives
#
if [ -f "sahara-prog_A-prog_B.cpio" ]; then
	assert_programmer "sahara-prog_A-prog_B.cpio" 0 \
	    "  13: prog_A.elf" \
	    "  14: prog_B.elf"
else
    printf "%-60s SKIP\n" "sahara-prog_A-prog_B.cpio"
fi

if [ -f "sahara-invalid-id.cpio" ]; then
	assert_programmer "sahara-invalid-id.cpio" 1 ""
else
    printf "%-60s SKIP\n" "sahara-invalid-id.cpio"
fi

#
# Clean up
#
rm -f 13a:prog_A.elf \
      13prog_A.elf \
      c:\\prog_D.elf \
      prog_A.elf \
      prog_B.elf \
      prog_C.elf \
      sahara-absolute-path.xml \
      sahara-empty-path.xml \
      sahara-invalid-id.cpio \
      sahara-invalid-id.xml \
      sahara-non-exist.xml \
      sahara-prog_A-prog_B.cpio \
      sahara-prog_A-prog_B.xml \
      sahara-windows.xml

exit $ANY_FAIL
