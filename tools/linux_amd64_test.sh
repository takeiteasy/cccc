#!/bin/sh
# linux_amd64_test.sh [colima-profile] [image] [jobs]
#
# 5-way sharded test run inside the Linux amd64 Colima container, each shard
# under a 300s timeout (#850, mirrors tools/Makefile.backup's
# linux-x86_64-test recipe). Runs every shard even if an earlier one fails,
# then reports overall failure at the end -- the vendored build shell
# (src/build_shell.c) has no loop construct or variables to express the
# sharding or accumulate a fail flag the way this script's `for` loop does,
# so it's delegated to a real shell script rather than inlined into a
# RunCustom command string.
set +e

PROFILE="${1:-cccc-linux-amd64}"
IMAGE="${2:-cccc-linux-amd64}"
JOBS="${3:-8}"

rc=0
for pattern in 'test_[a-f]*.c' 'test_[g-l]*.c' 'test_[m-r]*.c' \
               'test_[s-u]*.c' 'test_[v-z]*.c'; do
    colima -p "$PROFILE" nerdctl -- run --rm --platform linux/amd64 \
        "$IMAGE" timeout 300 python3 tools/tests.py \
        --match "$pattern" --quiet -j "$JOBS" || rc=1
done
exit $rc
