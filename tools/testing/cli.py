"""CLI entry point for the single-suite CCCC test runner (tools/tests.py)."""

import argparse
import os
import subprocess
import sys
from pathlib import Path

from . import REPO_ROOT
from .platform import detect_platform
from .discovery import discover_tests
from .suite import _run_test_suite, merge_suite_results
from .report import print_summary
from .matrix import run_pass_matrix

# tests/suites/test_suite_posix.c's fork/signal-timing subtests are prone to
# scheduler-starvation flakiness when several other CPU-bound test-file
# processes run concurrently under -j (#853 -- confirmed by direct
# reproduction: running N copies of the compiled test binary concurrently
# fails intermittently, sequential runs never do). Running it in its own
# serial pass, sequenced after the rest of the parallel batch has finished,
# removes that contention for the one file that's sensitive to it.
ISOLATED_SERIAL_TESTS = frozenset({"test_suite_posix.c"})


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
        "--c4", action="store_true",
        help="Run the .c4 bytecode round-trip: compile each positive test to a .c4, then run it. "
             "Negative tests and a small set of FFI tests that cannot survive rehydration are skipped."
    )
    parser.add_argument(
        "--process-timeout", type=int, default=None, metavar="SECONDS",
        help="Wall-clock timeout in seconds for each test subprocess. Timed-out tests are "
             "reported as TIMEOUT failures. Useful for slow environments "
             "where processes may stall indefinitely. Default: no timeout."
    )
    return parser


def main():
    # Split argv at '--': everything after is forwarded verbatim to cccc.
    argv = sys.argv[1:]
    try:
        sep = argv.index("--")
        cccc_args = argv[sep + 1:]
        argv = argv[:sep]
    except ValueError:
        cccc_args = []

    parser = build_parser()
    args = parser.parse_args(argv)

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

    if not test_files:
        print(f"No test files found in {tests_dir}")
        sys.exit(1)

    n_jobs = args.jobs if args.jobs is not None else (os.cpu_count() or 1)

    use_leaks = args.leaks or args.profile_mem
    if use_leaks and platform == "unknown":
        print("Warning: Memory leak detection not supported on this platform")
        use_leaks = False

    if args.c4:
        if use_leaks:
            print("Warning: --leaks/--profile-mem are not supported in --c4 mode and will be ignored.")
            use_leaks = False
        if args.profile_cpu:
            print("Warning: --profile-cpu is not supported in --c4 mode and will be ignored.")
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
        if args.c4:
            print("C4 mode: compiling each positive test to .c4, then executing the bytecode")
        print(f"Using {n_jobs} parallel jobs")
        print("=======================")
        print()

    if args.matrix:
        ok = run_pass_matrix(cccc, script_dir, platform, cccc_args, n_jobs, args, test_files)
        sys.exit(0 if ok else 1)

    isolated_files = [t for t in test_files if t.name in ISOLATED_SERIAL_TESTS]
    parallel_files = [t for t in test_files if t.name not in ISOLATED_SERIAL_TESTS]

    if isolated_files and n_jobs > 1 and parallel_files:
        r = _run_test_suite(
            cccc, script_dir, use_leaks, platform, cccc_args,
            n_jobs, args, parallel_files,
        )
        r_isolated = _run_test_suite(
            cccc, script_dir, use_leaks, platform, cccc_args,
            1, args, isolated_files,
            header="[ isolated (serial) tests -- see #853 ]" if not args.quiet else None,
        )
        r = merge_suite_results(r, r_isolated)
    else:
        r = _run_test_suite(
            cccc, script_dir, use_leaks, platform, cccc_args,
            n_jobs, args, test_files,
        )

    ok = print_summary(r, args)
    sys.exit(0 if ok else 1)
