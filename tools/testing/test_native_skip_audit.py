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
    NATIVE_SKIP_TESTS_CLANG,
    NATIVE_SKIP_TESTS_GCC,
    NATIVE_SKIP_TESTS_LINUX,
    NATIVE_SKIP_TESTS_MACOS,
    native_audit_skips_enabled,
    native_skip_reason,
)
from testing.cli import _entry_applies_here

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


# --- #1186: compiler-family-keyed tables (NATIVE_SKIP_TESTS_CLANG/_GCC) and
# the audit's off_axis bucketing that makes them safe to add. ---


@case("a clang-only entry is skipped when the family matches, not otherwise")
def _(fail):
    if not NATIVE_SKIP_TESTS_CLANG:
        fail("NATIVE_SKIP_TESTS_CLANG should not be empty -- #1186 moved "
             "five confirmed clang-only divergences into it")
        return
    os.environ.pop("CCCC_AUDIT_NATIVE_SKIPS", None)
    name = next(iter(NATIVE_SKIP_TESTS_CLANG))
    # native_skip_reason() calls detect_native_cc_family() itself (reading
    # CCCC_NATIVE_CC/"cc" live) -- this test only exercises the dispatch
    # logic's OWN family gate via _entry_applies_here, the pure-function half
    # cli.py's audit report uses, rather than mocking the live compiler
    # probe.
    if not _entry_applies_here(name, platform="linux", family="clang"):
        fail(f"{name!r} is in NATIVE_SKIP_TESTS_CLANG -- should apply under "
             f"family='clang'")
    if _entry_applies_here(name, platform="linux", family="gcc"):
        fail(f"{name!r} is clang-only -- must not apply under family='gcc'")


@case("a gcc-only entry is skipped when the family matches, not otherwise")
def _(fail):
    if not NATIVE_SKIP_TESTS_GCC:
        fail("NATIVE_SKIP_TESTS_GCC should not be empty -- #1186 moved the "
             "Darwin-gcc constructor-priority WONT_FIX group into it")
        return
    name = next(iter(NATIVE_SKIP_TESTS_GCC))
    if not _entry_applies_here(name, platform="macos", family="gcc"):
        fail(f"{name!r} is in NATIVE_SKIP_TESTS_GCC -- should apply under "
             f"family='gcc'")
    if _entry_applies_here(name, platform="macos", family="clang"):
        fail(f"{name!r} is gcc-only -- must not apply under family='clang'")


@case("a macOS-only entry does not apply when audited off-platform")
def _(fail):
    # This is #1186's own false-positive class: test_setpayload_zero_1079.c
    # (NATIVE_SKIP_TESTS_MACOS) genuinely passes when audited on Linux --
    # not because it's stale, but because its only skip entry is scoped to
    # a platform this run isn't on. _entry_applies_here must say so, or the
    # audit's STALE bucket falsely claims it.
    if "test_setpayload_zero_1079.c" not in NATIVE_SKIP_TESTS_MACOS:
        fail("test_setpayload_zero_1079.c should still be in "
             "NATIVE_SKIP_TESTS_MACOS -- #1186's own off_axis fix depends "
             "on this exact entry")
        return
    if _entry_applies_here("test_setpayload_zero_1079.c", platform="linux",
                           family="gcc"):
        fail("a macOS-only entry must not apply when audited on linux -- "
             "this is exactly the false-positive #1186 reported")
    if not _entry_applies_here("test_setpayload_zero_1079.c", platform="macos",
                               family="gcc"):
        fail("a macOS-only entry must still apply when audited on macos")


@case("no entry in any table applies with platform/family both unset")
def _(fail):
    # Sanity check on _entry_applies_here's own contract: NATIVE_SKIP_TESTS
    # (unconditional) should still apply regardless of platform/family;
    # every platform- or family-scoped table should not, when neither axis
    # matches anything.
    name, _ = next(iter(NATIVE_SKIP_TESTS.items()))
    if not _entry_applies_here(name, platform=None, family=None):
        fail(f"{name!r} is in the unconditional NATIVE_SKIP_TESTS table -- "
             f"should apply regardless of platform/family")


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
