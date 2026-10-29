#!/bin/sh
# linux_amd64_msan_test.sh [colima-profile] [image] [jobs]
#
# Build cccc-msan and run the suites against it inside the Linux amd64
# Colima container, using `./cccc --build build.c --build-target=test_msan`.
#
# NOTE (#844): MSan reports roughly half the targets as failed from an
# uninstrumented libc/libffi blind spot, documented in docs/test-infrastructure.md. A
# nonzero exit here is expected, not on its own a regression signal --
# compare the failure count/names against that documented baseline.
set -e

PROFILE="${1:-cccc-linux-amd64}"
IMAGE="${2:-cccc-linux-amd64}"
JOBS="${3:-8}"

colima -p "$PROFILE" nerdctl -- run --rm --security-opt seccomp=unconfined --platform linux/amd64 "$IMAGE" sh -c "
    ./cccc --build build.c --build-target=test_msan --build-jobs=$JOBS --build-keep-going
"
