"""CLI entry point for the single-suite CCCC test runner (tools/tests.py)."""

import argparse
import os
import subprocess
import sys
from pathlib import Path

from . import (
    NATIVE_SKIP_TESTS,
    NATIVE_SKIP_TESTS_CLANG,
    NATIVE_SKIP_TESTS_GCC,
    NATIVE_SKIP_TESTS_GCC_MACOS,
    NATIVE_SKIP_TESTS_LINUX,
    NATIVE_SKIP_TESTS_MACOS,
    REPO_ROOT,
)
from .platform import detect_native_cc_family, detect_platform
from .discovery import discover_tests
from .suite import run_test_suite_with_isolation
from .report import print_summary
from .matrix import run_pass_matrix


def build_parser():
    parser = argparse.ArgumentParser(description="Test runner for CCCC")
    parser.add_argument(
        "--matrix", action="store_true",
        help="Run the full test suite once per individual -f optimization pass "
             "(9 runs: baseline, 7 passes, stress) and show a per-pass attribution table"
    )
    parser.add_argument(
        "--quiet", action="store_true",
        help="Suppress per-test output; only show final summary"
    )
    parser.add_argument(
        "--leaks", action="store_true", help="Enable memory leak detection"
    )
    parser.add_argument("--match", help="Filter tests by pattern")
    parser.add_argument(
        "--suites", action="store_true",
        help="Run only [[cccc::test]] framework suites (tests/suites/)"
    )
    parser.add_argument(
        "--legacy", action="store_true",
        help="Run only legacy single-file tests (tests/, excluding tests/suites/)"
    )
    parser.add_argument(
        "-j", "--jobs", type=int, default=8, help="Number of parallel jobs"
    )
    parser.add_argument(
        "--asan", action="store_true", help="Use cccc-asan binary (AddressSanitizer + UBSan)"
    )
    parser.add_argument(
        "--ubsan", action="store_true", help="Use cccc-ubsan binary (UndefinedBehaviorSanitizer)"
    )
    parser.add_argument(
        "--tsan", action="store_true", help="Use cccc-tsan binary (ThreadSanitizer)"
    )
    parser.add_argument(
        "--msan", action="store_true", help="Use cccc-msan binary (MemorySanitizer, Linux-only)"
    )
    parser.add_argument(
        "--binary", help="Path to cccc binary (overrides all other binary options)"
    )
    parser.add_argument(
        "--bench", action="store_true", help="Report per-test execution time"
    )
    parser.add_argument(
        "--profile-cpu", action="store_true",
        help="Run tests under gperftools CPU profiler (builds cccc-prof if needed)"
    )
    parser.add_argument(
        "--profile-mem", action="store_true",
        help="Run tests with enhanced memory profiling (macOS: leaks+heap, Linux: valgrind)"
    )
    parser.add_argument(
        "--vm-profile", action="store_true",
        help="Collect per-test VM opcode profile JSON under profile/vm-opcodes"
    )
    parser.add_argument(
        "--native", action="store_true",
        help="Run the -c=native serializer round-trip: compile each eligible test with -c=native, "
             "then run the resulting binary and check its exit code. EXPECT_COMPILE_ERROR tests "
             "assert compile failure; EXPECT_RUNTIME_ERROR and diagnostic tests assert compile "
             "success only. --build, --testing, and VM-only-safety-flag tests are skipped -- see "
             "man/TESTING.md's 'Native round-trip mode' section."
    )
    parser.add_argument(
        "--native-audit-skips", action="store_true",
        help="#1182: behavioural audit of NATIVE_SKIP_TESTS/NATIVE_SKIP_TESTS_MACOS/ "
             "NATIVE_SKIP_TESTS_LINUX (tools/testing/__init__.py) -- implies --native, "
             "bypasses all three tables (the --build/-c/-o/frontend-mode/VM-only-flag "
             "skip checks still apply), and restricts the corpus to just the files "
             "those tables name. A file that now passes has a stale skip entry to "
             "delete; man/TESTING.md's 'Native round-trip mode' section has the full "
             "writeup."
    )
    parser.add_argument(
        "--process-timeout", type=int, default=None, metavar="SECONDS",
        help="Wall-clock timeout in seconds for each test subprocess. Timed-out tests are "
             "reported as TIMEOUT failures. Useful for slow environments "
             "where processes may stall indefinitely. Default: no timeout."
    )
    return parser


def main(argv=None):
    # argv defaults to sys.argv[1:] for the ordinary CLI entry point
    # (tools/tests.py); an explicit list lets a caller in-process (e.g.
    # run_tests.py's native-skip-audit sub-suite, #1182) invoke this with
    # its own arguments without spawning a subprocess.
    if argv is None:
        argv = sys.argv[1:]
    # Split argv at '--': everything after is forwarded verbatim to cccc.
    try:
        sep = argv.index("--")
        cccc_args = argv[sep + 1:]
        argv = argv[:sep]
    except ValueError:
        cccc_args = []

    parser = build_parser()
    args = parser.parse_args(argv)

    if args.native_audit_skips:
        # Set before any worker process (multiprocessing pool) spawns, so
        # every one inherits it -- native_skip_reason() (tools/testing/
        # __init__.py) reads this at import time in each worker, since it's
        # called from deep inside per-test subprocess isolation, not
        # threaded, and can't be threaded a plain function parameter
        # through the whole run_test_suite_with_isolation call chain for a
        # flag that's off in every ordinary run.
        os.environ["CCCC_AUDIT_NATIVE_SKIPS"] = "1"
        args.native = True

    script_dir = REPO_ROOT

    # Binary selection (precedence: --binary > sanitizer flags > default)
    if args.binary:
        cccc = Path(args.binary).resolve()
    elif args.asan:
        cccc = script_dir / "cccc-asan"
    elif args.ubsan:
        cccc = script_dir / "cccc-ubsan"
    elif args.tsan:
        cccc = script_dir / "cccc-tsan"
    elif args.msan:
        cccc = script_dir / "cccc-msan"
    else:
        cccc = script_dir / "cccc"

    tests_dir = script_dir / "tests"

    if not cccc.exists():
        binary_name = cccc.name
        print(f"Error: {binary_name} not found.")
        if args.binary or args.asan or args.ubsan or args.tsan or args.msan:
            print("Please build the requested target first (e.g., 'make asan').")
        else:
            print("Please run 'make' first.")
        sys.exit(1)

    if not tests_dir.exists():
        print("Error: tests directory not found.")
        sys.exit(1)

    platform = detect_platform()

    test_files = discover_tests(
        tests_dir,
        match=args.match,
        suites=args.suites,
        legacy=args.legacy,
    )

    if args.native_audit_skips:
        # Restrict the corpus to exactly the files the six hardcoded tables
        # name -- the audit's whole point is "does the skip for THIS file
        # still hold", not a full native corpus run (seconds instead of
        # minutes). NATIVE_SKIP_TESTS_MACOS/_LINUX/_CLANG/_GCC/_GCC_MACOS are
        # included even off their platform/family: a stale platform- or
        # family-only entry is still worth reporting (it just can't be
        # deleted here without also confirming the other platform/family,
        # see man/TESTING.md) -- and #1186 needs the *other* direction here
        # too: a foreign-axis entry that passes here is expected, not stale
        # (see _print_native_skip_audit's off_axis bucket below).
        audited_names = (
            set(NATIVE_SKIP_TESTS)
            | set(NATIVE_SKIP_TESTS_MACOS)
            | set(NATIVE_SKIP_TESTS_LINUX)
            | set(NATIVE_SKIP_TESTS_CLANG)
            | set(NATIVE_SKIP_TESTS_GCC)
            | set(NATIVE_SKIP_TESTS_GCC_MACOS)
        )
        test_files = [t for t in test_files if t.name in audited_names]

    if not test_files:
        print(f"No test files found in {tests_dir}")
        sys.exit(1)

    n_jobs = args.jobs if args.jobs is not None else (os.cpu_count() or 1)

    use_leaks = args.leaks or args.profile_mem
    if use_leaks and platform == "unknown":
        print("Warning: Memory leak detection not supported on this platform")
        use_leaks = False

    if args.native:
        if args.matrix:
            print("Error: --native cannot be combined with --matrix (native doesn't use the VM optimization pipeline).")
            sys.exit(1)
        if use_leaks:
            print("Warning: --leaks/--profile-mem are not supported in --native mode and will be ignored.")
            use_leaks = False
        if args.profile_cpu:
            print("Warning: --profile-cpu is not supported in --native mode and will be ignored.")
            args.profile_cpu = False

    # CPU profiling: use cccc-prof if available/requested
    if args.profile_cpu:
        cccc_prof = script_dir / "cccc-prof"
        if not cccc_prof.exists():
            print("cccc-prof not found. Building it now...")
            build_result = subprocess.run(
                ["make", "profile-cpu-build"], capture_output=True, text=True, cwd=script_dir
            )
            if build_result.returncode != 0 or not cccc_prof.exists():
                print("Error: failed to build cccc-prof. Run 'make profile-cpu-build' manually.")
                sys.exit(1)
        cccc = cccc_prof
        if not args.quiet:
            print("CPU profiling enabled (using cccc-prof)")

    if not args.quiet:
        print(f"Running CCCC tests using: {cccc.name}")
        if use_leaks:
            leak_tools = {"macos": "leaks", "linux": "valgrind", "windows": "drmemory"}
            print(
                f"Memory leak detection enabled (using '{leak_tools.get(platform, '?')}')"
            )
        if args.profile_mem:
            print("Memory profiling enabled (enhanced leak + heap tracking)")
        if args.bench:
            print("Benchmarking mode: per-test timing enabled")
        if getattr(args, "vm_profile", False):
            vm_profile_dir = script_dir / "profile" / "vm-opcodes"
            print(f"VM opcode profiling enabled (JSON: {vm_profile_dir})")
        if args.match:
            print(f"Filtering tests matching: {args.match}")
        if args.native:
            print("Native mode: compiling each eligible test with -c=native, then executing the binary")
        print(f"Using {n_jobs} parallel jobs")
        print("=======================")
        print()

    if args.matrix:
        ok = run_pass_matrix(cccc, script_dir, platform, cccc_args, n_jobs, args, test_files)
        sys.exit(0 if ok else 1)

    r = run_test_suite_with_isolation(
        cccc, script_dir, use_leaks, platform, cccc_args,
        n_jobs, args, test_files,
    )

    ok = print_summary(r, args)

    if args.native_audit_skips:
        # The audit's own pass/fail contract is narrower than print_summary's:
        # this restricted 38-file corpus is EXPECTED to be mostly skips/
        # failures (a "still failing" or "refused by design" entry is
        # pre-existing, known state, not a new regression) -- only a STALE
        # entry (passes with its skip bypassed) is something this mode
        # should ever fail CI over, so it overrides ok rather than ANDing
        # with it.
        ok = _print_native_skip_audit(r, test_files, platform, detect_native_cc_family())

    sys.exit(0 if ok else 1)


# #1182: the v1 --testing exclusions (test_setup/teardown hooks, negative
# error=/expect_compile_error= tests -- man/TESTING.md's "Native round-trip
# mode" section) are enforced only by their NATIVE_SKIP_TESTS entry, so
# bypassing the table under --native-audit-skips routes them into an actual
# -c=native compile, which the compiler itself refuses with a clear
# diagnostic (by design, not a bug the audit should ever ask to "fix"). Kept
# as an explicit list rather than pattern-matching the refusal diagnostic
# text, since that text is an implementation detail this list doesn't need
# to track.
_NATIVE_AUDIT_REFUSED_BY_DESIGN = frozenset({
    "test_hook_inherit_per_test.c", "test_hook_inherit_stacked_once.c",
    "test_hook_inherit_reentry.c", "test_hook_inherit_prefix_guard.c",
    "test_hook_inherit_once.c", "test_suite_attributes.c",
    "test_suite_testing_framework.c", "test_suite_compile_errors.c",
    "test_suite_stack_safety.c", "test_suite_std_c17.c",
})

# #1182/#1184: a skip entry whose underlying bug is a genuine data race
# (atomic_fetch_add()'s non-atomic load-then-store, #1184) intermittently
# PASSES on any given run -- confirmed ~87-93% pass rate under stress. A
# plain pass/fail classification would call that STALE most of the time,
# hard-failing run_tests.py's audit sub-suite on a bogus finding rather than
# the real, still-open bug. Named here rather than silently excluded so the
# report says why; delete a name from here only once its ticket actually
# lands and NATIVE_SKIP_TESTS' own entry is removed in the same change.
_NATIVE_AUDIT_KNOWN_FLAKY = frozenset({
    "test_threads_call_once_1088.c",
})


def _entry_applies_here(name, platform, family):
    """True if `name` has a skip-table entry that actually governs it on
    THIS run's platform+compiler-family combination -- i.e. deleting that
    entry, if the file now passes, would actually be correct here. #1186:
    NATIVE_SKIP_TESTS_MACOS/_LINUX/_CLANG/_GCC entries deliberately stay in
    the audited corpus even off their own axis (see the corpus-restriction
    comment above), so a name can appear in the corpus with NO entry that
    applies here at all -- that is the off_axis case _print_native_skip_audit
    splits out below, not a staleness finding.
    """
    if name in NATIVE_SKIP_TESTS:
        return True
    if platform == "macos" and name in NATIVE_SKIP_TESTS_MACOS:
        return True
    if platform == "linux" and name in NATIVE_SKIP_TESTS_LINUX:
        return True
    if family == "clang" and name in NATIVE_SKIP_TESTS_CLANG:
        return True
    if family == "gcc" and name in NATIVE_SKIP_TESTS_GCC:
        return True
    if (platform == "macos" and family == "gcc"
            and name in NATIVE_SKIP_TESTS_GCC_MACOS):
        return True
    return False


def _print_native_skip_audit(r, test_files, platform, family=None):
    """Classify every file in the --native-audit-skips corpus as STALE
    (passed with the table bypassed, AND governed by an entry that applies
    on this platform/family -- delete that entry), off_axis (passed, but
    every skip-table entry naming this file is scoped to a *different*
    platform or compiler family than this run -- an expected pass, not a
    staleness finding: #1186's own false-positive class, e.g.
    test_setpayload_zero_1079.c is macOS-only and correctly passes when
    audited on Linux), KEPT (still skips/fails for a reason independent of
    the table -- a --build/-c/-o/frontend-mode/VM-only-flag check, or a v1
    --testing exclusion refused by the compiler itself), or a genuine
    native failure worth investigating before deciding. Returns False if
    anything looks like a real (non-refused-by-design) failure or an
    actionable STALE entry, so CI can hard-fail on it -- off_axis never
    does.
    """
    def basename_of(entry):
        # native_skipped_tests are already skip_reason dicts with a real
        # name; failed_tests are "{name} (REASON)" strings (suite.py).
        name = entry["test_name"] if isinstance(entry, dict) else entry.split(" (", 1)[0]
        return Path(name).name

    skipped_names = {basename_of(e) for e in r.get("native_skipped_tests", [])}
    failed_names = {basename_of(e) for e in r.get("failed_tests", [])}
    crashed_names = {basename_of(e) for e in r.get("crashed_tests", [])}

    audited = sorted({t.name for t in test_files})
    stale, off_axis, kept_skip, refused_by_design, known_flaky, real_failures = (
        [], [], [], [], [], [])
    for name in audited:
        if name in _NATIVE_AUDIT_KNOWN_FLAKY:
            known_flaky.append(name)
        elif name in failed_names or name in crashed_names:
            (refused_by_design if name in _NATIVE_AUDIT_REFUSED_BY_DESIGN
             else real_failures).append(name)
        elif name in skipped_names:
            kept_skip.append(name)
        elif _entry_applies_here(name, platform, family):
            stale.append(name)
        else:
            off_axis.append(name)

    print()
    print("=== --native-audit-skips report (#1182) ===")
    if stale:
        print(f"STALE ({len(stale)}) -- now pass; delete the skip entry:")
        for name in stale:
            print(f"  {name}")
    if off_axis:
        print(f"PASSES here, but every entry naming it is scoped to a "
              f"different platform/compiler family ({len(off_axis)}) -- "
              f"not actionable on this run:")
        for name in off_axis:
            print(f"  {name}")
    if kept_skip:
        print(f"KEPT, still hits an independent skip check ({len(kept_skip)}):")
        for name in kept_skip:
            print(f"  {name}")
    if refused_by_design:
        print(f"KEPT, refused by the compiler by design, not a bug ({len(refused_by_design)}):")
        for name in refused_by_design:
            print(f"  {name}")
    if known_flaky:
        print(f"KEPT, known-flaky (intermittent, tracked by its own ticket, "
              f"not a staleness signal either way) ({len(known_flaky)}):")
        for name in known_flaky:
            print(f"  {name}")
    if real_failures:
        print(f"STILL FAILING, not yet resolved ({len(real_failures)}):")
        for name in real_failures:
            print(f"  {name}")
    if not any((stale, off_axis, kept_skip, refused_by_design, known_flaky, real_failures)):
        print("(no audited files matched the discovered corpus)")
    print("============================================")

    return not stale
