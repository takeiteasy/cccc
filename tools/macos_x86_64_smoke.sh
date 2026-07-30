#!/bin/sh
# macos_x86_64_smoke.sh <cross-binary-path>
#
# Rosetta smoke test for the macOS x86_64 cross-compiled cccc binary (#850,
# mirrors tools/Makefile.backup's macos-x86_64-smoke recipe). Invoked from
# build.c's macos_x86_64_smoke build target via RunCustom -- delegated to a
# real shell script (rather than inlined into the RunCustom command string)
# because it needs $(...) command substitution, $? exit-code capture, and a
# trap, none of which the vendored build shell (src/build_shell.c) supports.
# Requires Rosetta 2 (/usr/bin/arch -x86_64) to be installed.
set -eu

BIN="$1"
if [ -z "$BIN" ]; then
    echo "usage: $0 <cross-binary-path>" >&2
    exit 1
fi

machine=$(/usr/bin/arch -x86_64 /usr/bin/uname -m)
echo "Rosetta machine: $machine"
test "$machine" = "x86_64"

rc=0
/usr/bin/arch -x86_64 "$BIN" -I./include tests/test_fortytwo.c || rc=$?
test "$rc" -eq 42

rc=0
CCCC_NATIVE_CC=/usr/bin/clang /usr/bin/arch -x86_64 \
    "$BIN" -I./include --asm-passthru tests/test_asm_passthru.c || rc=$?
test "$rc" -eq 42

tmp=$(mktemp /tmp/cccc-native-x86_64.XXXXXX)
trap 'rm -f "$tmp"' EXIT
CCCC_NATIVE_CC=/usr/bin/clang /usr/bin/arch -x86_64 \
    "$BIN" -c=native -o "$tmp" tests/test_fortytwo.c
file "$tmp"
file "$tmp" | grep -q 'x86_64'
rc=0
/usr/bin/arch -x86_64 "$tmp" || rc=$?
test "$rc" -eq 42

echo "macos-x86_64-smoke: OK"
