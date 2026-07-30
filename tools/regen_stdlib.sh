#!/bin/sh
# regen_stdlib.sh -- regenerate src/std.c from tools/generate_stdlib.c, using
# the compiler binary given as $1.
#
# Ported from the Makefile's `stdlib` target (mktemp + cmp + mv) so build.c's
# two-pass stdlib_gen step (#842, #571) can reuse the same atomic recipe. This
# has to be a real shell script rather than a `RunCustom` shell command: the
# regen must be atomic (see below), and the vendored shell used by RunCustom
# (src/build_shell.c) has no set -e, mktemp, or trap builtins.
#
# Why atomic: `./cccc -G ... > src/std.c` would truncate the committed
# 406 KB src/std.c the instant the shell opens the redirect, before the
# generator writes a single byte -- so a failed or interrupted regen (a bad
# -I, a crash mid-generation) would leave src/std.c empty or partial with no
# way back once src/std.c is no longer tracked by git. Instead: generate into
# a scratch temp file, compare, and only replace src/std.c with `mv` (an
# atomic rename on the same filesystem) if the content actually changed. An
# unchanged regen leaves src/std.c's mtime untouched too, which matters if
# --build-cache is ever enabled for this graph (its Level-1 check is mtime
# based).
set -e

CCCC_BIN=${1:?"usage: regen_stdlib.sh <path-to-cccc-binary>"}

tmp=$(mktemp src/std.c.tmp.XXXXXX)
trap 'rm -f "$tmp"' EXIT

"$CCCC_BIN" -G -I./include tools/generate_stdlib.c > "$tmp"

if cmp -s "$tmp" src/std.c; then
    rm -f "$tmp"
else
    mv "$tmp" src/std.c
fi
