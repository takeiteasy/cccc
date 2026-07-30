#!/bin/sh
# macos_x86_64_test.sh <cross-binary-path> [jobs]
#
# Full test run for the macOS x86_64 cross-compiled cccc binary under
# Rosetta (#850, mirrors tools/Makefile.backup's macos-x86_64-test recipe).
# Unlike the smoke test, a failure in one phase doesn't stop the rest --
# every phase runs and the script exits nonzero if any of them failed.
set +e

BIN="$1"
JOBS="${2:-8}"
if [ -z "$BIN" ]; then
    echo "usage: $0 <cross-binary-path> [jobs]" >&2
    exit 1
fi

rc=0
/usr/bin/arch -x86_64 /usr/bin/python3 tools/tests.py --binary "$BIN" -j "$JOBS" || rc=1
/usr/bin/arch -x86_64 /usr/bin/python3 tools/tests.py --binary "$BIN" --c4 -j "$JOBS" || rc=1
/usr/bin/arch -x86_64 /usr/bin/python3 tools/test_host_signal_debugger.py --binary "$BIN" || rc=1
exit $rc
