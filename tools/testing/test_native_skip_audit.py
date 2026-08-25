#!/usr/bin/env python3
"""Unit tests for tools/testing/__init__.py's #1182 skip-audit bypass (#1182).

Pure in-process tests against native_skip_reason()/native_audit_skips_enabled()
-- no cccc binary needed. Run as its own audited sub-suite from
tools/run_tests.py (mirrors test_header_parse.py's own main() -> 0/nonzero
convention), and can also be run standalone:
`python3 tools/testing/test_native_skip_audit.py`.

The property under test is the fall-through, not early-return, invariant:
CCCC_AUDIT_NATIVE_SKIPS=1 must bypass ONLY the three hardcoded skip-table
lookups in native_skip_reason(), still falling through to the same
--build/-c/-o/frontend-mode/VM-only-safety-flag checks every other test
gets. A prior draft of this bypass early-returned None instead, which would
silently let a --build test (no bytecode/native artifact to compile at all)
through the audit's restricted corpus as a false "STALE" finding.
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from testing import (
    NATIVE_SKIP_TESTS,
    NATIVE_SKIP_TESTS_LINUX,
    NATIVE_SKIP_TESTS_MACOS,
    native_audit_skips_enabled,
    native_skip_reason,
)

CASES = []


def case(name):
    def deco(fn):
        CASES.append((name, fn))
        return fn
    return deco


@case("table lookup applies when the audit flag is unset")
def _(fail):
    os.environ.pop("CCCC_AUDIT_NATIVE_SKIPS", None)
    if native_audit_skips_enabled():
        fail("native_audit_skips_enabled() should be False with no env var set")
    name, reason = next(iter(NATIVE_SKIP_TESTS.items()))
    if native_skip_reason(name, [], [], platform="linux") != reason:
        fail(f"expected the table's own reason for {name!r} with the audit flag unset")


@case("audit flag bypasses NATIVE_SKIP_TESTS, falls through to no other check")
def _(fail):
    os.environ["CCCC_AUDIT_NATIVE_SKIPS"] = "1"
    try:
        if not native_audit_skips_enabled():
            fail("native_audit_skips_enabled() should be True once the env var is set")
        name = next(iter(NATIVE_SKIP_TESTS))
        got = native_skip_reason(name, [], [], platform="linux")
        if got is not None:
            fail(f"expected the table entry to be bypassed (None), got {got!r}")
    finally:
        os.environ.pop("CCCC_AUDIT_NATIVE_SKIPS", None)


@case("audit flag bypasses NATIVE_SKIP_TESTS_MACOS the same way")
def _(fail):
    if not NATIVE_SKIP_TESTS_MACOS:
        return
    os.environ["CCCC_AUDIT_NATIVE_SKIPS"] = "1"
    try:
        name = next(iter(NATIVE_SKIP_TESTS_MACOS))
        got = native_skip_reason(name, [], [], platform="macos")
        if got is not None:
            fail(f"expected the macOS table entry to be bypassed (None), got {got!r}")
    finally:
        os.environ.pop("CCCC_AUDIT_NATIVE_SKIPS", None)


@case("audit flag bypasses NATIVE_SKIP_TESTS_LINUX the same way")
def _(fail):
    if not NATIVE_SKIP_TESTS_LINUX:
        return
    os.environ["CCCC_AUDIT_NATIVE_SKIPS"] = "1"
    try:
        name = next(iter(NATIVE_SKIP_TESTS_LINUX))
        got = native_skip_reason(name, [], [], platform="linux")
        if got is not None:
            fail(f"expected the Linux table entry to be bypassed (None), got {got!r}")
    finally:
        os.environ.pop("CCCC_AUDIT_NATIVE_SKIPS", None)


@case("audit flag still falls through to the --build skip check")
def _(fail):
    os.environ["CCCC_AUDIT_NATIVE_SKIPS"] = "1"
    try:
        got = native_skip_reason(
            "test_not_in_any_skip_table.c", [], ["--build"], platform="linux"
        )
        if got is None:
            fail(
                "a --build test must still be skipped under the audit bypass "
                "(fall-through, not early-return, invariant) -- got None"
            )
    finally:
        os.environ.pop("CCCC_AUDIT_NATIVE_SKIPS", None)


@case("audit flag still lets an ordinary test through with no skip reason")
def _(fail):
    os.environ["CCCC_AUDIT_NATIVE_SKIPS"] = "1"
    try:
        got = native_skip_reason(
            "test_not_in_any_skip_table.c", [], [], platform="linux"
        )
        if got is not None:
            fail(f"expected no skip reason for an ordinary test, got {got!r}")
    finally:
        os.environ.pop("CCCC_AUDIT_NATIVE_SKIPS", None)


def main():
    failures = []
    for name, fn in CASES:
        errors = []
        fn(errors.append)
        if errors:
            failures.append((name, errors))

    if not failures:
        print(f"test_native_skip_audit: {len(CASES)} cases passed")
        return 0

    for name, errors in failures:
        print(f"FAILED: {name}")
        for e in errors:
            print(f"    {e}")
    print(f"\n{len(failures)}/{len(CASES)} cases failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
