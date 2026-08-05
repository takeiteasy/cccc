#!/bin/sh
# linux_amd64_msan_test.sh [colima-profile] [image] [jobs]
#
# Build cccc-msan and run the full suite against it inside the Linux amd64
# Colima container (#850, mirrors tools/Makefile.backup's
# linux-x86_64-msan-test recipe -- updated for the post-#842 build.c flow:
# the Makefile's `make cccc-msan` has no equivalent in the root Makefile
# anymore, so this uses `build/cccc --build build.c
# --build-target=cccc_msan` instead).
#
# NOTE (#844): MSan reports ~262/700 known false positives from an
# uninstrumented libc/libffi blind spot, documented in man/TESTING.md. A
# nonzero exit here is expected, not on its own a regression signal --
# compare the failure count/names against that documented baseline.
set -e

PROFILE="${1:-cccc-linux-amd64}"
IMAGE="${2:-cccc-linux-amd64}"
JOBS="${3:-8}"

colima -p "$PROFILE" nerdctl -- run --rm --platform linux/amd64 "$IMAGE" sh -c "
    build/cccc --build build.c --build-target=cccc_msan &&
    python3 tools/tests.py --binary build/cccc-msan --quiet -j $JOBS
"
