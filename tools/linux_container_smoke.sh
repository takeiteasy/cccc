#!/bin/sh
# linux_container_smoke.sh <colima-profile> <image> <platform> <uname-arch> <file-grep-pattern>
#
# Smoke test inside a Linux container: verify the container's own `uname -m`
# matches <uname-arch>, verify ./cccc matches <file-grep-pattern> under
# `file`, and run a trivial exit-42 program (#850, mirrors
# tools/Makefile.backup's linux-{x86_64,aarch64}-smoke recipes). Shared by
# both architectures rather than duplicated, since the two Makefile recipes
# differed only in these five values. Delegated to a real shell script (not
# inlined into a RunCustom command string) because it needs $(...) command
# substitution and $? exit-code capture, which the vendored build shell
# (src/build_shell.c) doesn't support.
set -eu

PROFILE="$1"
IMAGE="$2"
PLATFORM="$3"
UNAME_ARCH="$4"
FILE_PATTERN="$5"
if [ -z "$FILE_PATTERN" ]; then
    echo "usage: $0 <colima-profile> <image> <platform> <uname-arch> <file-grep-pattern>" >&2
    exit 1
fi

colima -p "$PROFILE" nerdctl -- run --rm --platform "$PLATFORM" "$IMAGE" sh -c "
    machine=\$(uname -m)
    echo \"Container machine: \$machine\"
    test \"\$machine\" = \"$UNAME_ARCH\"
    file ./cccc
    file ./cccc | grep -Eq \"$FILE_PATTERN\"
    rc=0
    ./cccc -I./include tests/test_fortytwo.c || rc=\$?
    test \"\$rc\" -eq 42
"
echo "linux-$UNAME_ARCH-smoke ($IMAGE): OK"
