#!/usr/bin/env python3
"""Unified CCCC test orchestrator.

Runs all sub-suites in sequence and reports a unified pass/fail summary.
Exit code is non-zero if any sub-suite fails.

Sub-suites:
  source              — main test suite (tools/testing/ package)
  c4                  — .c4 bytecode round-trip (compile → save → reload → run)
  native              — -c=native serializer round-trip (ticket #1157; on by
                        default, --no-native opts out -- see man/TESTING.md's
                        "Native round-trip mode" section)
  native_skip_audit   — behavioural staleness audit of NATIVE_SKIP_TESTS/
                        NATIVE_SKIP_TESTS_MACOS/NATIVE_SKIP_TESTS_LINUX/
                        NATIVE_SKIP_TESTS_CLANG/NATIVE_SKIP_TESTS_GCC/
                        NATIVE_SKIP_TESTS_GCC_MACOS, hard-fails on any
                        stale entry (ticket #1182)
  debugger            — macOS host-signal crash-debugger integration (macOS only)
  repl                — interactive REPL PTY integration (POSIX only, ticket #661)
  debugger_condition  — conditional breakpoint PTY integration (POSIX only, ticket 113)
  debugger_print      — debugger `print`/`p` command PTY integration (POSIX only, #958)
  sqlite              — SQLite 3.53.2 amalgamation smoke test (skips if zip absent)
  header_resolution_smoke — CCCC header resolution from a foreign CWD (ticket #891)
  host_attribute_link_smoke — host __attribute__-stripping duplicate-symbol
                        link regression under a real gcc (ticket #1199);
                        skips when no real (non-clang) gcc is on PATH
  comptime_native_smoke — native (-m/-c=generated/-c=native) serializer regressions (tickets #892/#897/#901/#904/#918)
  smoke_skip_audit    — behavioural staleness audit of comptime_native_smoke.py's
                        own SMOKE_CASE_SKIPS_GCC_MACOS (ticket #1197); reports
                        "nothing to audit" on any platform/family other than
                        macOS+gcc, since that's the only axis the table covers
  audit_ffi           — src/stdlib FFI registration audit (ticket #784)
  audit_test_headers  — tests/**/*.c CCCC_*/EXPECT_* header directive damage audit (ticket #1153)
  test_header_parse   — tools/testing/header.py parse_test_header() unit tests (ticket #1153)
                        and tools/audit_test_headers.py audit_file() typo/near-miss unit
                        tests (ticket #1158)
  test_native_skip_audit — native_skip_reason() fall-through-invariant unit tests (ticket #1182)
  test_proc_wedge     — run_capture() timeout/group-kill/stdin-closing unit tests (ticket #1185)
  test_wedge          — wedge.py deadline-watchdog/SIGUSR1-dump/progress-log unit tests (ticket #1202)
  reflection_ffi_check — reflection.h FFI table generation freshness (ticket #859)
  shims_check          — src/shims.inc freshness vs src/shims/*.c
  audit_reflection_enums — reflection.h enum values vs internal enums (ticket #860)
  fuzz                — fuzz regression corpus replay, compile-only (ticket #625)

Optional:
  --bench  — run the cross-compiler benchmark after the test suites
  --perf   — run an instrumented VM-opcode pass over tests/benchmarks/ (ticket #625)

Usage:
  python3 tools/run_tests.py [-j N] [--binary PATH] [--quiet] [--process-timeout N]
  python3 tools/run_tests.py --bench               # also run bench.py after tests
  python3 tools/run_tests.py --perf                # also run the perf sub-suite
  ./cccc --build build.c --build-target=test       # build + run everything (calls this script)
"""

import argparse
import json
import os
import subprocess
import sys
import types
from pathlib import Path

# Ensure tools/ is on sys.path so the 'testing' package is importable.
_TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_TOOLS_DIR))

from testing import REPO_ROOT
from testing.platform import detect_platform
from testing.discovery import discover_tests
from testing.suite import run_test_suite_with_isolation
from testing.report import print_summary
from testing.proc import run_capture
from testing import wedge

# #1186: the native round-trip suite and its skip-table staleness audit
# (#1157/#1182) were originally wired in as hard-blocking; the first real
# sr.ht Linux push surfaced failures neither macOS nor the cccc-linux-amd64
# verification container reproduced, and both were downgraded to advisory as
# a stopgap. Root cause turned out to be compiler *family* (clang vs. gcc),
# not GCC version as first hypothesized -- the verification container's gcc
# simply predates GCC 14's promotion of -Wincompatible-pointer-types to a
# hard error, so it never exercised that half of the split at all. Fixed:
# the genuine divergences (FTS/DIR/DBM opaque-handle spelling disagreeing
# with the replayed host header, two bundled headers/the generated test
# harness illegal under strict C89, a genuine anonymous-struct duplicate-
# emission bug in the serializer) got real fixes; the legitimately
# compiler-family-specific ones got moved into NATIVE_SKIP_TESTS_CLANG/_GCC
# (tools/testing/__init__.py) alongside the existing _MACOS/_LINUX tables,
# and _print_native_skip_audit (tools/testing/cli.py) learned a fourth
# bucket -- an entry scoped to a different platform/family that happens to
# pass here is "off_axis", not STALE, which is what made #1182's own audit
# throw six false positives in the first place. Both empty locally now
# (`CCCC_NATIVE_CC=clang`/`=gcc-16 python3 tools/tests.py
# --native-audit-skips` both report zero actionable STALE) -- flipped back
# to blocking here on that basis.
#
# #1193: the very next sr.ht push (build 1872614, this same #1186 commit)
# DID go red on both suites -- exactly the risk the acceptance clause above
# was hedging against. Investigated rather than reflexively re-downgraded:
# native_skip_audit's red was NATIVE_SKIP_TESTS_GCC's five constructor/
# destructor-priority entries lacking a platform axis (they're gcc-on-
# *Darwin* specific, not universal gcc, so they wrongly "applied" on sr.ht's
# Linux gcc where they actually pass) -- split into NATIVE_SKIP_TESTS_GCC_
# MACOS. native's red was two real, now-fixed bugs (a vector global losing
# its natural alignment under gcc/Darwin's own -c=native output, and a
# genuine UB varargs bug in the test itself) plus one pre-existing, distinct
# gap (test_math_c23_ieee.c's C23 IEC 60559 output, #1195) quarantined via a
# new NATIVE_SKIP_TESTS_LINUX entry. Both suites deliberately stay blocking
# here rather than being re-downgraded -- see #1193 for the still-open,
# two-consecutive-green-pushes watch this fix is waiting to satisfy (the
# clock is back at zero: this same push is the first candidate). Re-add a
# name here immediately (not in a follow-up ticket) if a push goes red for a
# reason NOT already covered by one of the skip tables above.
_ADVISORY_SUITES = frozenset()


def _make_suite_args(quiet=True, bench=False, c4=False, native=False,
                     vm_profile=False, process_timeout=None):
    """Return a SimpleNamespace compatible with _run_test_suite's args parameter."""
    return types.SimpleNamespace(
        quiet=quiet,
        bench=bench,
        c4=c4,
        native=native,
        vm_profile=vm_profile,
        process_timeout=process_timeout,
    )


def _run_source_suite(cccc, n_jobs, quiet, process_timeout):
    """Run the main source-mode test suite. Returns (r_dict, ok)."""
    platform = detect_platform()
    tests_dir = REPO_ROOT / "tests"
    test_files = discover_tests(tests_dir)
    args = _make_suite_args(quiet=quiet, process_timeout=process_timeout)
    if not quiet:
        print(f"Running source suite ({len(test_files)} tests)…")
    r = run_test_suite_with_isolation(cccc, REPO_ROOT, False, platform, [], n_jobs, args, test_files)
    ok = r["failed"] == 0 and r["crashed"] == 0
    return r, ok


def _run_c4_suite(cccc, n_jobs, quiet, process_timeout):
    """Run the .c4 bytecode round-trip suite. Returns (r_dict, ok)."""
    platform = detect_platform()
    tests_dir = REPO_ROOT / "tests"
    test_files = discover_tests(tests_dir)
    args = _make_suite_args(quiet=quiet, c4=True, process_timeout=process_timeout)
    if not quiet:
        print(f"Running c4 round-trip suite ({len(test_files)} tests)…")
    r = run_test_suite_with_isolation(cccc, REPO_ROOT, False, platform, [], n_jobs, args, test_files)
    ok = r["failed"] == 0 and r["crashed"] == 0
    return r, ok


def _run_native_suite(cccc, n_jobs, quiet, process_timeout):
    """Run the -c=native serializer round-trip suite (#1157). Returns
    (r_dict, ok). detect_platform() is threaded through native_skip_reason
    (via run_single_test's own platform-detection call) exactly as it is
    for a plain `tools/tests.py --native` invocation -- see man/TESTING.md's
    "Native round-trip mode" section for the three tiers this drives and
    NATIVE_SKIP_TESTS/NATIVE_SKIP_TESTS_MACOS (tools/testing/__init__.py)
    for the known, tracked divergences it skips.
    """
    platform = detect_platform()
    tests_dir = REPO_ROOT / "tests"
    test_files = discover_tests(tests_dir)
    args = _make_suite_args(quiet=quiet, native=True, process_timeout=process_timeout)
    if not quiet:
        print(f"Running native round-trip suite ({len(test_files)} tests)…")
    r = run_test_suite_with_isolation(cccc, REPO_ROOT, False, platform, [], n_jobs, args, test_files)
    ok = r["failed"] == 0 and r["crashed"] == 0
    return r, ok


def _run_native_skip_audit_suite(cccc, n_jobs, process_timeout=None):
    """Run the #1182 behavioural skip-table audit (--native-audit-skips) and
    hard-fail if it reports a stale entry. A subprocess (not an in-process
    call like the other pure-Python audits below) because
    --native-audit-skips sets CCCC_AUDIT_NATIVE_SKIPS in its own process
    environment (tools/testing/cli.py) -- isolating that avoids leaking it
    into any sub-suite that runs after this one in the same process.
    Returns (status_str, ok).

    #1185: this used to be an unbounded subprocess.run with no --process-
    timeout forwarded to the inner tests.py invocation -- an unbounded wait
    stacked on an unbounded wait, immune to the caller's own timeout. Both
    ends now go through run_capture (tools/testing/proc.py), which kills the
    whole process group -- the inner tests.py AND its own -j-wide worker
    pool -- rather than leaving grandchildren to hold the pipe open.
    """
    cmd = [sys.executable, str(_TOOLS_DIR / "tests.py"), "--native-audit-skips",
           "--binary", str(cccc), "-j", str(n_jobs), "--quiet"]
    if process_timeout:
        cmd += ["--process-timeout", str(process_timeout)]
    try:
        # A little slack over the inner --process-timeout: that bounds one
        # test subprocess, not the whole -j-wide audit run.
        outer_timeout = (process_timeout + 120) if process_timeout else None
        result = run_capture(cmd, timeout=outer_timeout)
    except subprocess.TimeoutExpired:
        return "audit subprocess timed out (wedged) -- see #1185", False
    if result.returncode == 0:
        return ("no stale NATIVE_SKIP_TESTS/NATIVE_SKIP_TESTS_MACOS/"
                "NATIVE_SKIP_TESTS_LINUX/NATIVE_SKIP_TESTS_CLANG/"
                "NATIVE_SKIP_TESTS_GCC/NATIVE_SKIP_TESTS_GCC_MACOS entries",
                True)
    if "STALE (" in result.stdout:
        # A real staleness finding from _print_native_skip_audit -- the
        # expected failure mode this sub-suite exists to catch. #1193: a
        # blind splitlines()[-40:] tail used to be reported here -- on a
        # real run the STALE section prints FIRST (_print_native_skip_
        # audit's bucket order), so a run with enough off_axis/kept/
        # refused-by-design/still-failing output below it silently pushed
        # the actual STALE names out of the tail. Extract the STALE
        # section specifically (from its own header to the next bucket
        # header or the report's closing rule) instead of guessing at a
        # line count. The `"STALE (" in result.stdout` check above doesn't
        # guarantee the match starts a LINE (a bucket name could in theory
        # appear inside an entry's own free-text reason) -- next(..., None)
        # degrades to the old blind tail instead of a StopIteration crash
        # if that ever happens.
        lines = result.stdout.splitlines()
        start = next((i for i, l in enumerate(lines) if l.startswith("STALE (")),
                     None)
        if start is None:
            tail = "\n".join(lines[-40:])
            return f"stale skip entries found (see man/TESTING.md):\n{tail}", False
        end = start + 1
        while end < len(lines) and not (
                lines[end].startswith(("PASSES here", "KEPT,", "STILL FAILING"))
                or lines[end].startswith("====")):
            end += 1
        stale_section = "\n".join(lines[start:end])
        return f"stale skip entries found (see man/TESTING.md):\n{stale_section}", False
    # Any other nonzero exit (binary missing, harness crash, import error,
    # incompatible flags) is NOT a staleness finding -- report it as such so
    # a future maintainer doesn't go hunting a skip table that's fine.
    tail = "\n".join((result.stdout + result.stderr).splitlines()[-40:])
    return f"audit subprocess failed unexpectedly (exit {result.returncode}), not a staleness finding:\n{tail}", False


def _run_host_signal_suite(cccc):
    """Run the macOS host-signal debugger integration tests.

    Returns (status_str, ok) where status_str is 'passed'/'failed'/'skipped'.
    """
    if sys.platform != "darwin":
        return "skipped (macOS-only)", True

    script = _TOOLS_DIR / "test_host_signal_debugger.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        # Import and call directly to avoid spawning a new interpreter.
        # The script's main() returns 0 or 1 and never calls sys.exit inside.
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_host_signal_debugger", script)
        mod = importlib.util.module_from_spec(spec)

        # Override the global CCCC so the imported module uses our binary.
        mod.__dict__["_OVERRIDE_CCCC"] = str(cccc)
        spec.loader.exec_module(mod)

        # Inject --binary argument so the argparse in main() picks it up.
        old_argv = sys.argv
        sys.argv = ["test_host_signal_debugger.py", "--binary", str(cccc)]
        try:
            rc = mod.main()
        finally:
            sys.argv = old_argv

        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_repl_suite(cccc):
    """Run the interactive REPL PTY integration tests (ticket #661).

    Returns (status_str, ok) where status_str is 'passed'/'failed'/'skipped'.
    The REPL is a stdin-driven interactive session (readline, multi-line
    continuation prompts), so it needs a real pseudo-terminal to exercise --
    the `pty` module is POSIX-only, hence the Windows skip (matching the
    host-signal suite's platform-gating pattern above).
    """
    if sys.platform == "win32":
        return "skipped (POSIX-only, needs a pty)", True

    script = _TOOLS_DIR / "test_repl.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_repl", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        old_argv = sys.argv
        sys.argv = ["test_repl.py", "--binary", str(cccc)]
        try:
            rc = mod.main()
        finally:
            sys.argv = old_argv

        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_debugger_condition_suite(cccc):
    """Run the conditional-breakpoint PTY integration tests (ticket 113).

    Returns (status_str, ok) where status_str is 'passed'/'failed'/'skipped'.
    Same pty-required rationale as the REPL suite above.
    """
    if sys.platform == "win32":
        return "skipped (POSIX-only, needs a pty)", True

    script = _TOOLS_DIR / "test_debugger_condition.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_debugger_condition", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        old_argv = sys.argv
        sys.argv = ["test_debugger_condition.py", "--binary", str(cccc)]
        try:
            rc = mod.main()
        finally:
            sys.argv = old_argv

        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_debugger_print_suite(cccc):
    """Run the debugger `print`/`p` command PTY integration tests (#958).

    Returns (status_str, ok) where status_str is 'passed'/'failed'/'skipped'.
    Same pty-required rationale as the REPL/debugger_condition suites above.
    """
    if sys.platform == "win32":
        return "skipped (POSIX-only, needs a pty)", True

    script = _TOOLS_DIR / "test_debugger_print.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_debugger_print", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        old_argv = sys.argv
        sys.argv = ["test_debugger_print.py", "--binary", str(cccc)]
        try:
            rc = mod.main()
        finally:
            sys.argv = old_argv

        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_sqlite_suite(cccc):
    """Run the SQLite amalgamation smoke test.

    Returns (status_str, ok). Skips cleanly if the zip is absent.
    """
    script = _TOOLS_DIR / "sqlite_smoke.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("sqlite_smoke", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        # Redirect the CCCC binary used by sqlite_smoke by temporarily
        # patching the module-level 'cccc' variable used in main().
        # sqlite_smoke.main() computes root/cccc from __file__; since
        # we imported via spec there is no __file__ root difference,
        # but we let main() run normally (it will use REPO_ROOT / "cccc").
        # If a custom --binary was passed we need to swap it in.
        rc = mod.main()
        if rc == 0:
            return "passed (or skipped: zip absent)", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_header_resolution_suite():
    """Run the header resolution smoke tests (#891).

    Invokes the built cccc from a temp directory with no include flags, so
    it exercises resolution paths tools/tests.py's own -I./include can't
    reach. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "header_resolution_smoke.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("header_resolution_smoke", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_host_attribute_link_smoke_suite():
    """Run the host __attribute__-stripping duplicate-symbol link smoke
    test (#1199).

    Only exercises anything under a real (non-clang) gcc -- clang was never
    affected, so this reports "skipped" everywhere that isn't a macOS/Linux
    host with a genuine gcc on PATH. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "host_attribute_link_smoke.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "host_attribute_link_smoke", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed (or skipped: no real gcc on PATH)", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_comptime_native_suite():
    """Run the comptime/native serializer smoke tests (#892).

    The serializer that reconstructs a runtime translation unit only runs
    under -m/-c=generated/-c=native, none of which the VM-only source suite exercises,
    so a serializer regression like #892 (two distinct opaque struct types
    collapsing into "the same type") needs its own smoke test. Returns
    (status_str, ok).
    """
    script = _TOOLS_DIR / "comptime_native_smoke.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("comptime_native_smoke", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_smoke_skip_audit_suite(process_timeout=None):
    """Run comptime_native_smoke.py's own #1197 staleness audit
    (--audit-skips) and hard-fail if it reports a stale
    SMOKE_CASE_SKIPS_GCC_MACOS entry. A subprocess, not an in-process
    importlib call like _run_comptime_native_suite() above, for the exact
    same reason _run_native_skip_audit_suite() is a subprocess: --audit-skips
    sets CCCC_AUDIT_NATIVE_SKIPS in its own process environment, and
    isolating that avoids leaking it into any suite that runs after this one
    in the same process (comptime_native_smoke's own ordinary run included,
    since run_tests.py loads that one in-process). Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "comptime_native_smoke.py"
    if not script.exists():
        return "skipped (script not found)", True

    cmd = [sys.executable, str(script), "--audit-skips"]
    try:
        result = run_capture(cmd, timeout=(process_timeout + 120) if process_timeout else None)
    except subprocess.TimeoutExpired:
        return "audit subprocess timed out (wedged)", False
    if result.returncode == 0:
        # Either every SMOKE_CASE_SKIPS_GCC_MACOS entry still fails with the
        # table bypassed (justified), or nothing here applies to this
        # platform/family at all (true on every CI machine that exists
        # today -- the table only covers macOS+gcc). Report the "nothing to
        # audit"/"KEPT, ..." summary line, not audit_skips()'s closing
        # "====" rule -- the last non-empty line when a case actually ran
        # (and printed its own multi-line FAIL detail first) is that rule,
        # not the verdict.
        lines = result.stdout.splitlines()
        summary = next((l for l in lines
                         if l.startswith("nothing to audit")
                         or l.startswith("KEPT,")), None)
        return summary or "no stale SMOKE_CASE_SKIPS_GCC_MACOS entries", True
    if "STALE (" in result.stdout:
        lines = result.stdout.splitlines()
        start = next((i for i, l in enumerate(lines) if l.startswith("STALE (")), None)
        if start is None:
            tail = "\n".join(lines[-40:])
            return f"stale skip entries found (see man/TESTING.md):\n{tail}", False
        end = start + 1
        while end < len(lines) and not lines[end].startswith(("KEPT,", "====")):
            end += 1
        stale_section = "\n".join(lines[start:end])
        return f"stale skip entries found (see man/TESTING.md):\n{stale_section}", False
    tail = "\n".join((result.stdout + result.stderr).splitlines()[-40:])
    return f"audit subprocess failed unexpectedly (exit {result.returncode}), not a staleness finding:\n{tail}", False


def _run_audit_ffi_suite():
    """Run the src/stdlib FFI registration audit (#784).

    Pure source scan -- no cccc binary needed. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "audit_ffi.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("audit_ffi", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_audit_test_headers_suite():
    """Run the tests/**/*.c header-directive damage audit (#1153).

    Pure source scan -- no cccc binary needed. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "audit_test_headers.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("audit_test_headers", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        # argv=[] (not None, which would fall through to run_tests.py's own
        # sys.argv) -- default testdir "tests", hard-fail mode.
        rc = mod.main([])
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_header_parse_unit_tests():
    """Run tools/testing/test_header_parse.py's parse_test_header() unit
    tests (#1153) and audit_test_headers.py audit_file() typo/near-miss
    unit tests (#1158). Pure in-memory tests -- no cccc binary needed.
    Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "testing" / "test_header_parse.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_header_parse", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_native_skip_audit_unit_tests():
    """Run tools/testing/test_native_skip_audit.py's fall-through-invariant
    unit tests (#1182). Pure in-process tests against native_skip_reason()/
    native_audit_skips_enabled() -- no cccc binary needed. Returns
    (status_str, ok).
    """
    script = _TOOLS_DIR / "testing" / "test_native_skip_audit.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_native_skip_audit", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_proc_wedge_unit_tests():
    """Run tools/testing/test_proc_wedge.py's run_capture() unit tests
    (#1185). Spawns real (but short-lived, deterministic) subprocesses to
    exercise the timeout/group-kill/stdin-closing behavior -- no cccc
    binary needed. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "testing" / "test_proc_wedge.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_proc_wedge", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_wedge_unit_tests():
    """Run tools/testing/test_wedge.py's wedge.py unit tests (#1202).
    Deterministic, no cccc binary, no container needed. Returns
    (status_str, ok).
    """
    script = _TOOLS_DIR / "testing" / "test_wedge.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_wedge", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_audit_reflection_enums_suite():
    """Run reflection.h TypeKind/NodeKind/AttrTargetKind vs internal enum
    value audit (#860).

    Pure source scan -- no cccc binary needed. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "audit_reflection_enums.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("audit_reflection_enums", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_reflection_ffi_check():
    """Check src/reflection_ffi_{protos,register}.inc are up to date with
    include/cccc/reflection.h (#859).

    Pure source scan -- no cccc binary needed. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "gen_reflection_ffi.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("gen_reflection_ffi", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main(check=True)
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_shims_check():
    """Check src/shims.inc is up to date with src/shims/*.c.

    Pure source scan -- no cccc binary needed. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "gen_shims.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("gen_shims", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main(check=True)
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_fuzz_suite(cccc):
    """Run the fuzz regression corpus replay (#625).

    Compile-only against tests/fuzz/corpus/. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "fuzz_replay.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("fuzz_replay", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main(["--binary", str(cccc)])
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_perf_suite(cccc):
    """Run an instrumented VM-opcode pass over tests/benchmarks/*.c (#625).

    Informational only -- always returns ok=True; perf numbers on a laptop
    or shared CI runner are too noisy to threshold on. Writes per-benchmark
    JSON to profile/perf/ and prints an aggregate summary: total instructions
    retired per benchmark and the top-N hottest opcodes across the set.
    """
    bench_dir = REPO_ROOT / "tests" / "benchmarks"
    sources = sorted(bench_dir.glob("*.c"))
    if not sources:
        return "skipped (no benchmark sources)", True

    out_dir = REPO_ROOT / "profile" / "perf"
    out_dir.mkdir(parents=True, exist_ok=True)

    totals = {}
    opcode_totals = {}
    for src in sources:
        try:
            r = subprocess.run(
                [str(cccc), "--vm-profile", "--json", "-I./include", str(src)],
                cwd=REPO_ROOT, capture_output=True, text=True, timeout=120,
            )
        except subprocess.TimeoutExpired:
            print(f"  {src.name}: timeout")
            continue

        # Guest stdout (e.g. the benchmark's own "result: N" line) precedes
        # the profiler's JSON blob on stdout -- find where the JSON starts.
        brace = r.stdout.find("{")
        if brace == -1:
            print(f"  {src.name}: no profile JSON produced")
            continue

        try:
            data = json.loads(r.stdout[brace:])
        except json.JSONDecodeError:
            print(f"  {src.name}: malformed profile JSON")
            continue

        out_path = out_dir / f"{src.stem}.json"
        with open(out_path, "w") as f:
            json.dump(data, f, indent=2)

        total_opcodes = data.get("total_opcodes", 0)
        totals[src.stem] = total_opcodes
        for op in data.get("opcodes", []):
            opcode_totals[op["opcode"]] = opcode_totals.get(op["opcode"], 0) + op["count"]

        print(f"  {src.name}: {total_opcodes:,} opcodes retired -> {out_path.relative_to(REPO_ROOT)}")

    if totals:
        print()
        print("  Total instructions retired per benchmark:")
        for name, count in sorted(totals.items(), key=lambda kv: -kv[1]):
            print(f"    {name:<15} {count:>15,}")

        print()
        print("  Top 10 hottest opcodes across the set:")
        for opcode, count in sorted(opcode_totals.items(), key=lambda kv: -kv[1])[:10]:
            print(f"    {opcode:<15} {count:>15,}")

    return f"reported ({len(totals)}/{len(sources)} benchmarks)", True


def _run_bench(cccc):
    """Run the cross-compiler benchmark as a subprocess (bench.py uses sys.exit)."""
    script = _TOOLS_DIR / "bench.py"
    if not script.exists():
        return "skipped (bench.py not found)", True

    result = subprocess.run(
        [sys.executable, str(script), "--cccc", str(cccc)],
        cwd=REPO_ROOT,
    )
    if result.returncode == 0:
        return "passed", True
    return "FAILED", False


def main():
    parser = argparse.ArgumentParser(
        description="Unified CCCC test orchestrator (runs all sub-suites)"
    )
    parser.add_argument(
        "-j", "--jobs", type=int, default=8, help="Parallel jobs for test suite"
    )
    parser.add_argument(
        "--binary", help="Path to cccc binary (used for all sub-suites)"
    )
    parser.add_argument(
        "--quiet", action="store_true",
        help="Suppress per-test output; only show sub-suite summaries"
    )
    parser.add_argument(
        "--process-timeout", type=int, default=600, metavar="SECONDS",
        help="Per-subprocess wall-clock timeout, forwarded to every sub-suite "
             "including the native_skip_audit shell-out (#1185: without a "
             "default, a single wedged test hangs the whole CI job until the "
             "build service's own top-level timeout kills it). "
             "Pass 0 to disable (unbounded, matching tools/tests.py's own "
             "default for interactive use). Default: 600."
    )
    parser.add_argument(
        "--phase-timeout", type=int, default=None, metavar="SECONDS",
        help="Deadline for each whole sub-suite/phase (#1202): if a phase "
             "doesn't finish in time, dump every thread's Python stack to "
             "build/wedge/traceback.log and hard-exit, instead of hanging "
             "until the CI service's own top-level job timeout kills the "
             "job with no evidence. Covers wedges --process-timeout can't "
             "(see tools/testing/wedge.py's module docstring for why), "
             "including the in-process importlib sub-suites that have no "
             "per-subprocess timeout at all. Defaults to "
             "max(process_timeout * 3, 1800) for the parallel source/c4/"
             "native suites and process_timeout + 300 for the rest, unless "
             "--process-timeout is 0 (unbounded), in which case no "
             "deadline is armed at all -- pass 0 explicitly to disable "
             "while keeping --process-timeout nonzero."
    )
    parser.add_argument(
        "--no-native", action="store_true",
        help="Skip the -c=native serializer round-trip suite (#1157; on by default -- "
             "see man/TESTING.md's 'Native round-trip mode' section)"
    )
    parser.add_argument(
        "--bench", action="store_true",
        help="Run the cross-compiler benchmark after the test suites"
    )
    parser.add_argument(
        "--perf", action="store_true",
        help="Run an instrumented VM-opcode pass over tests/benchmarks/ (informational, never fails)"
    )
    args = parser.parse_args()

    # #1202: sr.ht's CI manifest invokes plain `python3` (no -u, no
    # PYTHONUNBUFFERED), so stdout is block-buffered (~8KB) writing to the
    # job's log pipe -- the "last line printed" in a hung job's log can
    # trail real progress by up to a full buffer, which is exactly what made
    # #1202's original "wedge happens near test_attr_vector_size_*" clue an
    # artifact rather than evidence. Force line buffering so the log always
    # reflects real progress. errors="backslashreplace" closes a second,
    # unrelated hazard: print_single_result (tools/testing/suite.py) prints
    # emoji, and a UnicodeEncodeError raised inside on_done's
    # add_done_callback is swallowed there -- next_to_print would then never
    # advance and the log would silently stop mid-suite while the run kept
    # going underneath, mimicking a wedge without being one.
    sys.stdout.reconfigure(line_buffering=True, errors="backslashreplace")
    sys.stderr.reconfigure(line_buffering=True, errors="backslashreplace")

    if args.binary:
        cccc = Path(args.binary).resolve()
    else:
        cccc = REPO_ROOT / "cccc"

    if not cccc.exists():
        print(f"Error: {cccc.name} not found. Run 'make' first.", file=sys.stderr)
        sys.exit(1)

    n_jobs = args.jobs if args.jobs else (os.cpu_count() or 1)
    quiet = args.quiet
    timeout = args.process_timeout or None

    # #1202: external deadline watchdog, armed per-phase below. Only makes
    # sense when --process-timeout is itself bounded -- with it disabled
    # (0, "unbounded, interactive use") a phase deadline would fight that
    # same intent, so leave no deadline armed at all in that case unless the
    # caller passes --phase-timeout explicitly.
    if args.phase_timeout is not None:
        phase_timeout = args.phase_timeout or None  # 0 -> disabled
    elif timeout:
        phase_timeout = max(timeout * 3, 1800)
    else:
        phase_timeout = None
    scalar_phase_timeout = (
        args.phase_timeout if args.phase_timeout is not None
        else (timeout + 300 if timeout else None)
    )
    wedge_dir = wedge.install(REPO_ROOT)

    print(f"=== CCCC Test Orchestrator ===")
    print(f"Binary: {cccc}")
    print(f"Jobs:   {n_jobs}")
    if phase_timeout or scalar_phase_timeout:
        print(f"Wedge watchdog: armed per-phase, dumps to {wedge_dir}/ "
              f"(kill -USR1 {os.getpid()} for a live dump)")
    print()

    suite_results = {}

    # --- Source suite ---
    print("[ source suite ]")
    wedge.arm("source", phase_timeout)
    r_src, ok_src = _run_source_suite(cccc, n_jobs, quiet, timeout)
    print_summary(r_src, _make_suite_args(quiet=quiet))
    suite_results["source"] = ok_src

    # --- C4 round-trip suite ---
    print()
    print("[ c4 round-trip suite ]")
    wedge.arm("c4", phase_timeout)
    r_c4, ok_c4 = _run_c4_suite(cccc, n_jobs, quiet, timeout)
    print_summary(r_c4, _make_suite_args(quiet=quiet, c4=True))
    suite_results["c4"] = ok_c4

    # --- Native round-trip suite (#1157) ---
    if not args.no_native:
        print()
        print("[ native round-trip suite ]")
        wedge.arm("native", phase_timeout)
        r_native, ok_native = _run_native_suite(cccc, n_jobs, quiet, timeout)
        print_summary(r_native, _make_suite_args(quiet=quiet, native=True))
        suite_results["native"] = ok_native

        # --- Native skip-table staleness audit (#1182) ---
        print()
        print("[ native_skip_audit ]")
        wedge.arm("native_skip_audit", scalar_phase_timeout)
        skip_audit_status, ok_skip_audit = _run_native_skip_audit_suite(cccc, n_jobs, timeout)
        print(f"  {skip_audit_status}")
        suite_results["native_skip_audit"] = ok_skip_audit

    # --- macOS host-signal debugger ---
    print()
    print("[ host-signal debugger ]")
    wedge.arm("debugger", scalar_phase_timeout)
    hsd_status, ok_hsd = _run_host_signal_suite(cccc)
    print(f"  {hsd_status}")
    suite_results["debugger"] = ok_hsd

    # --- REPL PTY integration ---
    print()
    print("[ repl integration ]")
    wedge.arm("repl", scalar_phase_timeout)
    repl_status, ok_repl = _run_repl_suite(cccc)
    print(f"  {repl_status}")
    suite_results["repl"] = ok_repl

    # --- Conditional breakpoint PTY integration ---
    print()
    print("[ debugger condition integration ]")
    wedge.arm("debugger_condition", scalar_phase_timeout)
    cond_status, ok_cond = _run_debugger_condition_suite(cccc)
    print(f"  {cond_status}")
    suite_results["debugger_condition"] = ok_cond

    # --- Debugger print command PTY integration (#958) ---
    print()
    print("[ debugger print integration ]")
    wedge.arm("debugger_print", scalar_phase_timeout)
    print_status, ok_print = _run_debugger_print_suite(cccc)
    print(f"  {print_status}")
    suite_results["debugger_print"] = ok_print

    # --- SQLite smoke ---
    print()
    print("[ sqlite smoke ]")
    wedge.arm("sqlite", scalar_phase_timeout)
    sqlite_status, ok_sqlite = _run_sqlite_suite(cccc)
    print(f"  {sqlite_status}")
    suite_results["sqlite"] = ok_sqlite

    # --- Header resolution smoke (#891) ---
    print()
    print("[ header_resolution_smoke ]")
    wedge.arm("header_resolution_smoke", scalar_phase_timeout)
    hdr_status, ok_hdr = _run_header_resolution_suite()
    print(f"  {hdr_status}")
    suite_results["header_resolution_smoke"] = ok_hdr

    # --- Host __attribute__-stripping duplicate-symbol link smoke (#1199) ---
    print()
    print("[ host_attribute_link_smoke ]")
    wedge.arm("host_attribute_link_smoke", scalar_phase_timeout)
    attr_link_status, ok_attr_link = _run_host_attribute_link_smoke_suite()
    print(f"  {attr_link_status}")
    suite_results["host_attribute_link_smoke"] = ok_attr_link

    # --- Comptime/native serializer smoke (#892) ---
    print()
    print("[ comptime_native_smoke ]")
    wedge.arm("comptime_native_smoke", scalar_phase_timeout)
    ctn_status, ok_ctn = _run_comptime_native_suite()
    print(f"  {ctn_status}")
    suite_results["comptime_native_smoke"] = ok_ctn

    # --- Smoke-case skip-table staleness audit (#1197) ---
    # Gated on --no-native, unlike comptime_native_smoke above: this sub-
    # suite's whole job is re-running skipped cases with CCCC_AUDIT_NATIVE_
    # SKIPS=1, i.e. exercising -c=native a second time, so it makes no sense
    # to run when --no-native says not to.
    if not args.no_native:
        print()
        print("[ smoke_skip_audit ]")
        wedge.arm("smoke_skip_audit", scalar_phase_timeout)
        smoke_audit_status, ok_smoke_audit = _run_smoke_skip_audit_suite(timeout)
        print(f"  {smoke_audit_status}")
        suite_results["smoke_skip_audit"] = ok_smoke_audit

    # --- FFI registration audit ---
    print()
    print("[ audit_ffi ]")
    wedge.arm("audit_ffi", scalar_phase_timeout)
    audit_status, ok_audit = _run_audit_ffi_suite()
    print(f"  {audit_status}")
    suite_results["audit_ffi"] = ok_audit

    # --- Test-header directive damage audit (#1153) ---
    print()
    print("[ audit_test_headers ]")
    wedge.arm("audit_test_headers", scalar_phase_timeout)
    hdr_audit_status, ok_hdr_audit = _run_audit_test_headers_suite()
    print(f"  {hdr_audit_status}")
    suite_results["audit_test_headers"] = ok_hdr_audit

    # --- Test-header parser unit tests (#1153) ---
    print()
    print("[ test_header_parse ]")
    wedge.arm("test_header_parse", scalar_phase_timeout)
    hdr_unit_status, ok_hdr_unit = _run_header_parse_unit_tests()
    print(f"  {hdr_unit_status}")
    suite_results["test_header_parse"] = ok_hdr_unit

    # --- Native skip-audit fall-through invariant unit tests (#1182) ---
    print()
    print("[ test_native_skip_audit ]")
    wedge.arm("test_native_skip_audit", scalar_phase_timeout)
    nsa_unit_status, ok_nsa_unit = _run_native_skip_audit_unit_tests()
    print(f"  {nsa_unit_status}")
    suite_results["test_native_skip_audit"] = ok_nsa_unit

    # --- proc.py run_capture() wedge-hardening unit tests (#1185) ---
    print()
    print("[ test_proc_wedge ]")
    wedge.arm("test_proc_wedge", scalar_phase_timeout)
    proc_unit_status, ok_proc_unit = _run_proc_wedge_unit_tests()
    print(f"  {proc_unit_status}")
    suite_results["test_proc_wedge"] = ok_proc_unit

    # --- wedge.py deadline-watchdog/SIGUSR1-dump unit tests (#1202) ---
    print()
    print("[ test_wedge ]")
    wedge.arm("test_wedge", scalar_phase_timeout)
    wedge_unit_status, ok_wedge_unit = _run_wedge_unit_tests()
    print(f"  {wedge_unit_status}")
    suite_results["test_wedge"] = ok_wedge_unit

    # --- Reflection FFI generation check (#859) ---
    print()
    print("[ reflection_ffi_check ]")
    wedge.arm("reflection_ffi_check", scalar_phase_timeout)
    refl_status, ok_refl = _run_reflection_ffi_check()
    print(f"  {refl_status}")
    suite_results["reflection_ffi_check"] = ok_refl

    # --- -c=native shim text generation check ---
    print()
    print("[ shims_check ]")
    wedge.arm("shims_check", scalar_phase_timeout)
    shims_status, ok_shims = _run_shims_check()
    print(f"  {shims_status}")
    suite_results["shims_check"] = ok_shims

    # --- Reflection enum parity audit (#860) ---
    print()
    print("[ audit_reflection_enums ]")
    wedge.arm("audit_reflection_enums", scalar_phase_timeout)
    enum_status, ok_enum = _run_audit_reflection_enums_suite()
    print(f"  {enum_status}")
    suite_results["audit_reflection_enums"] = ok_enum

    # --- Fuzz regression corpus replay ---
    print()
    print("[ fuzz replay ]")
    wedge.arm("fuzz", scalar_phase_timeout)
    fuzz_status, ok_fuzz = _run_fuzz_suite(cccc)
    print(f"  {fuzz_status}")
    suite_results["fuzz"] = ok_fuzz

    # --- Optional bench ---
    if args.bench:
        print()
        print("[ benchmark ]")
        wedge.arm("bench", scalar_phase_timeout)
        bench_status, ok_bench = _run_bench(cccc)
        print(f"  {bench_status}")
        suite_results["bench"] = ok_bench

    # --- Optional perf (VM opcode profile over tests/benchmarks/) ---
    if args.perf:
        print()
        print("[ perf ]")
        wedge.arm("perf", scalar_phase_timeout)
        perf_status, ok_perf = _run_perf_suite(cccc)
        print(f"  {perf_status}")
        suite_results["perf"] = ok_perf

    # #1202: everything that could wedge has finished -- cancel the deadline
    # watchdog before the summary/exit so it can't fire spuriously mid-print.
    wedge.disarm()

    # --- Unified summary ---
    print()
    print("=" * 50)
    print("  Unified Test Summary")
    print("=" * 50)
    all_ok = True
    for name, ok in suite_results.items():
        if not ok and name in _ADVISORY_SUITES:
            status = "⚠ FAIL (advisory)"
        else:
            status = "✓ PASS" if ok else "✗ FAIL"
        print(f"  {name:<12}  {status}")
        if not ok and name not in _ADVISORY_SUITES:
            all_ok = False
    print()
    if all_ok:
        print("All sub-suites passed.")
    else:
        failed = [n for n, ok in suite_results.items()
                  if not ok and n not in _ADVISORY_SUITES]
        print(f"FAILED sub-suites: {', '.join(failed)}")
    advisory_failed = [n for n, ok in suite_results.items()
                        if not ok and n in _ADVISORY_SUITES]
    if advisory_failed:
        print(f"ADVISORY (not blocking): {', '.join(advisory_failed)} "
              f"-- see #1186, man/TESTING.md")

    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
