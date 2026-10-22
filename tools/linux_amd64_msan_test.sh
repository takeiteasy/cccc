#!/bin/sh
# linux_amd64_msan_test.sh [colima-profile] [image] [jobs]
#
# Build cccc-msan and run the suites against it inside the Linux amd64
# Colima container, using `./cccc --build build.c --build-target=test_msan`.
#
# NOTE (#844): MSan reports ~262/700 known false positives from an
# uninstrumented libc/libffi blind spot, documented in docs/internal-testing.md. A
# nonzero exit here is expected, not on its own a regression signal --
# compare the failure count/names against that documented baseline.
set -e

PROFILE="${1:-cccc-linux-amd64}"
IMAGE="${2:-cccc-linux-amd64}"
JOBS="${3:-8}"

colima -p "$PROFILE" nerdctl -- run --rm --platform linux/amd64 "$IMAGE" sh -c "
    ./cccc --build build.c --build-target=test_msan --build-jobs=$JOBS --build-keep-going
"
