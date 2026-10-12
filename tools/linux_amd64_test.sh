#!/bin/sh
# linux_amd64_test.sh [colima-profile] [image] [jobs]
#
# Run the suites inside the Linux amd64 Colima container.
set -e

PROFILE="${1:-cccc-linux-amd64}"
IMAGE="${2:-cccc-linux-amd64}"
JOBS="${3:-8}"

colima -p "$PROFILE" nerdctl -- run --rm --platform linux/amd64 "$IMAGE" \
    timeout 600 ./cccc --build build.c --build-target=test \
    --build-jobs="$JOBS" --build-keep-going
