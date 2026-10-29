#!/bin/sh
# linux_amd64_msan_test.sh [colima-profile] [image] [jobs]
#
# Build cccc-msan and run the suites against it inside the Linux amd64
# Colima container, using `./cccc --build build.c --build-target=test_msan`.
#
# NOTE: host (libffi) results and out-parameters are unpoisoned in ops.c;
# see docs/test-infrastructure.md.
set -e

PROFILE="${1:-cccc-linux-amd64}"
IMAGE="${2:-cccc-linux-amd64}"
JOBS="${3:-8}"

colima -p "$PROFILE" nerdctl -- run --rm --security-opt seccomp=unconfined --platform linux/amd64 "$IMAGE" sh -c "
    ./cccc --build build.c --build-target=test_msan --build-jobs=$JOBS --build-keep-going
"
