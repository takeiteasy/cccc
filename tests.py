#!/usr/bin/env python3
"""Test runner for JCC.

Runs all test_*.c files in tests/ directory and reports results.
Supports parallel execution with -j/--jobs.
"""

import argparse
import concurrent.futures
import fnmatch
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def detect_platform():
    system = os.uname().sysname if hasattr(os, "uname") else os.name
    if system == "Darwin":
        return "macos"
    elif system == "Linux":
        return "linux"
    elif system in ("CYGWIN", "MINGW", "MSYS", "Windows"):
        return "windows"
    else:
        return "unknown"


def run_single_test(idx, test_file, jcc, script_dir, use_leaks, platform, jcc_args, bench=False):
    tests_dir = Path(script_dir) / "tests"
    test_name = str(test_file.relative_to(tests_dir))

    is_negative_test = False
    expects_runtime_error = False
    per_test_flags = []
    try:
        with open(test_file, "r") as f:
            first_line = f.readline()
            if "EXPECT_COMPILE_ERROR" in first_line:
                is_negative_test = True
            if "EXPECT_RUNTIME_ERROR" in first_line:
                expects_runtime_error = True
            if "JCC_FLAGS:" in first_line:
                flags_str = first_line.split("JCC_FLAGS:", 1)[1].strip().rstrip("*/").strip()
                per_test_flags = flags_str.split()
    except Exception:
        pass

    if use_leaks:
        if platform == "macos":
            normal_cmd = [str(jcc), "-I./include", *jcc_args, *per_test_flags, str(test_file)]
            normal_result = subprocess.run(
                normal_cmd, capture_output=True, text=True, cwd=script_dir
            )
            leak_cmd = [
                "leaks",
                "-atExit",
                "--",
                str(jcc),
                "-I./include",
                *jcc_args,
                *per_test_flags,
                str(test_file),
            ]
            leak_result = subprocess.run(
                leak_cmd, capture_output=True, text=True, cwd=script_dir
            )
            output = (
                normal_result.stdout
                + normal_result.stderr
                + leak_result.stdout
                + leak_result.stderr
            )
            exit_code = normal_result.returncode
            leak_output = leak_result.stdout + leak_result.stderr
            is_leaking = "0 leaks" not in leak_output
            cmd = None
        elif platform == "linux":
            cmd = [
                "valgrind",
                "--leak-check=full",
                "--error-exitcode=1",
                "--quiet",
                str(jcc),
                "-I./include",
                *jcc_args,
                *per_test_flags,
                str(test_file),
            ]
        elif platform == "windows":
            cmd = [
                "drmemory",
                "-batch",
                "-quiet",
                "--",
                str(jcc),
                "-I./include",
                *jcc_args,
                *per_test_flags,
                str(test_file),
            ]
        else:
            cmd = [str(jcc), "-I./include", *jcc_args, *per_test_flags, str(test_file)]
    else:
        cmd = [str(jcc), "-I./include", *jcc_args, *per_test_flags, str(test_file)]

    elapsed = None
    if cmd is not None:
        start = time.perf_counter()
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=script_dir)
        elapsed = time.perf_counter() - start
        output = result.stdout + result.stderr
        exit_code = result.returncode

        is_leaking = False
        if use_leaks and platform == "linux":
            if (
                "no leaks are possible" in output
                or "All heap blocks were freed" in output
            ):
                is_leaking = False
            elif (
                "definitely lost" in output
                or "indirectly lost" in output
                or "possibly lost" in output
            ):
                is_leaking = True
            else:
                is_leaking = False
        elif use_leaks and platform == "windows":
            if re.search(r"0 unique,.*0 total", output):
                is_leaking = False
            elif "LEAK" in output or "UNADDRESSABLE ACCESS" in output:
                is_leaking = True
            else:
                is_leaking = False

    has_compile_error = (
        "error generated" in output
        or "errors generated" in output
        or "cannot open file" in output
        or "undefined function" in output
        or "implicit declaration of a function" in output
        or ("expected" in output and "got" in output)
    )

    crashed = exit_code in (134, 139, 136, 141, -6, -11, -8, -13)

    if crashed:
        status = "crashed"
    elif is_leaking:
        status = "leak"
    elif has_compile_error:
        if is_negative_test:
            status = "negative_pass"
        else:
            status = "compile_error"
    elif exit_code == 42:
        status = "passed"
    elif expects_runtime_error and exit_code == 255:
        status = "negative_pass"
    else:
        status = "failed"

    return {
        "idx": idx,
        "test_name": test_name,
        "exit_code": exit_code,
        "status": status,
        "output": output,
        "is_negative_test": is_negative_test,
        "expects_runtime_error": expects_runtime_error,
        "elapsed": elapsed,
    }


def main():
    parser = argparse.ArgumentParser(description="Test runner for JCC")
    parser.add_argument(
        "--leaks", action="store_true", help="Enable memory leak detection"
    )
    parser.add_argument("--match", help="Filter tests by pattern")
    parser.add_argument(
        "-j", "--jobs", type=int, default=8, help="Number of parallel jobs"
    )
    parser.add_argument(
        "--asan", action="store_true", help="Use jcc-asan binary (AddressSanitizer + UBSan)"
    )
    parser.add_argument(
        "--ubsan", action="store_true", help="Use jcc-ubsan binary (UndefinedBehaviorSanitizer)"
    )
    parser.add_argument(
        "--tsan", action="store_true", help="Use jcc-tsan binary (ThreadSanitizer)"
    )
    parser.add_argument(
        "--msan", action="store_true", help="Use jcc-msan binary (MemorySanitizer, Linux-only)"
    )
    parser.add_argument(
        "--binary", help="Path to jcc binary (overrides all other binary options)"
    )
    parser.add_argument(
        "--bench", action="store_true", help="Report per-test execution time"
    )
    parser.add_argument(
        "--profile-cpu", action="store_true", help="Run tests under gperftools CPU profiler (builds jcc-prof if needed)"
    )
    parser.add_argument(
        "--profile-mem", action="store_true", help="Run tests with enhanced memory profiling (macOS: leaks+heap, Linux: valgrind)"
    )
    args, jcc_args = parser.parse_known_args()

    script_dir = Path(__file__).parent.resolve()

    if args.binary:
        jcc = Path(args.binary)
    elif args.asan:
        jcc = script_dir / "jcc-asan"
    elif args.ubsan:
        jcc = script_dir / "jcc-ubsan"
    elif args.tsan:
        jcc = script_dir / "jcc-tsan"
    elif args.msan:
        jcc = script_dir / "jcc-msan"
    else:
        jcc = script_dir / "jcc"
    tests_dir = script_dir / "tests"

    if not jcc.exists():
        binary_name = jcc.name
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

    test_files = sorted(
        f for f in tests_dir.rglob("test_*.c") if "failures" not in f.parts
    )

    if args.match:
        test_files = [f for f in test_files if fnmatch.fnmatch(f.name, args.match)]

    if not test_files:
        print(f"No test files found in {tests_dir}")
        sys.exit(1)

    if args.jobs is not None:
        n_jobs = args.jobs
    else:
        n_jobs = os.cpu_count() or 1

    use_leaks = args.leaks or args.profile_mem
    if use_leaks and platform == "unknown":
        print("Warning: Memory leak detection not supported on this platform")
        use_leaks = False

    # CPU profiling: use jcc-prof if available/requested
    if args.profile_cpu:
        jcc_prof = script_dir / "jcc-prof"
        if not jcc_prof.exists():
            print("jcc-prof not found. Building it now...")
            build_result = subprocess.run(
                ["make", "profile-cpu-build"], capture_output=True, text=True, cwd=script_dir
            )
            if build_result.returncode != 0 or not jcc_prof.exists():
                print("Error: failed to build jcc-prof. Run 'make profile-cpu-build' manually.")
                sys.exit(1)
        jcc = jcc_prof
        print("CPU profiling enabled (using jcc-prof)")

    print(f"Running JCC tests using: {jcc.name}")
    if use_leaks:
        leak_tools = {"macos": "leaks", "linux": "valgrind", "windows": "drmemory"}
        print(
            f"Memory leak detection enabled (using '{leak_tools.get(platform, '?')}')"
        )
    if args.profile_mem:
        print("Memory profiling enabled (enhanced leak + heap tracking)")
    if args.bench:
        print("Benchmarking mode: per-test timing enabled")
    if args.match:
        print(f"Filtering tests matching: {args.match}")
    print(f"Using {n_jobs} parallel jobs")
    print("=======================")
    print()

    test_args = [
        (i, test_file, jcc, str(script_dir), use_leaks, platform, jcc_args, args.bench)
        for i, test_file in enumerate(test_files)
    ]

    results = [None] * len(test_files)
    next_to_print = 0
    results_lock = threading.Lock()

    total = 0
    passed = 0
    failed = 0
    crashed = 0
    negative_passed = 0
    failed_tests = []
    crashed_tests = []

    def print_single_result(result):
        nonlocal total, passed, failed, crashed, negative_passed
        total += 1
        test_name = result["test_name"]
        status = result["status"]
        exit_code = result["exit_code"]
        output = result["output"]
        elapsed = result.get("elapsed")

        timing_str = ""
        if args.bench and elapsed is not None:
            timing_str = f" [{elapsed*1000:.1f}ms]"
            timings.append((test_name, elapsed))

        if status == "crashed":
            crashed += 1
            crashed_tests.append(f"{test_name} (exit code: {exit_code})")
            print(f"💥 {test_name} (CRASHED: exit code {exit_code}){timing_str}")
        elif status == "compile_error":
            failed += 1
            failed_tests.append(f"{test_name} (COMPILATION ERROR)")
            print(f"✗ {test_name} (COMPILATION ERROR){timing_str}")
            for line in output.splitlines()[:3]:
                print(f"  {line}")
        elif status == "leak":
            failed += 1
            failed_tests.append(f"{test_name} (MEMORY LEAK)")
            print(f"💧 {test_name} (MEMORY LEAK){timing_str}")
            leak_lines = [line for line in output.splitlines() if "Leak:" in line][:3]
            for line in leak_lines:
                print(f"  {line}")
        elif status == "negative_pass":
            negative_passed += 1
            if result["is_negative_test"]:
                print(f"✓ {test_name} (correctly rejected invalid code){timing_str}")
            else:
                print(f"✓ {test_name} (correctly detected runtime error){timing_str}")
        elif status == "passed":
            passed += 1
            print(f"✓ {test_name}{timing_str}")
        elif status == "failed":
            failed += 1
            failed_tests.append(f"{test_name} (exit code: {exit_code})")
            print(f"✗ {test_name} (expected exit code 42, got: {exit_code}){timing_str}")

    def flush_results():
        nonlocal next_to_print
        while next_to_print < len(results) and results[next_to_print] is not None:
            print_single_result(results[next_to_print])
            next_to_print += 1

    timings = []

    def on_done(future, idx):
        try:
            result = future.result()
        except Exception as e:
            tests_dir = Path(script_dir) / "tests"
            result = {
                "idx": idx,
                "test_name": str(test_files[idx].relative_to(tests_dir)),
                "exit_code": -1,
                "status": "crashed",
                "output": str(e),
                "is_negative_test": False,
                "expects_runtime_error": False,
                "elapsed": None,
            }

        with results_lock:
            results[idx] = result
            flush_results()

    with concurrent.futures.ThreadPoolExecutor(max_workers=n_jobs) as executor:
        futures = []
        for arg in test_args:
            future = executor.submit(run_single_test, *arg)
            future.add_done_callback(lambda f, idx=arg[0]: on_done(f, idx))
            futures.append(future)
        concurrent.futures.wait(futures)

    print()
    print("=======================")
    print("Test Results Summary")
    print("=======================")
    print(f"Total:          {total}")
    print(f"Passed:         {passed}")
    print(f"Negative tests: {negative_passed} (correctly rejected invalid code)")
    print(f"Failed:         {failed}")
    print(f"Crashed:        {crashed}")

    if crashed > 0:
        print()
        print("⚠️  CRASHED TESTS (segfaults/aborts):")
        for test in crashed_tests:
            print(f"  - {test}")

    if failed > 0:
        print()
        print("Failed tests:")
        for test in failed_tests:
            print(f"  - {test}")

    if args.bench and timings:
        print()
        print("=======================")
        print("Benchmark Results")
        print("=======================")
        timings.sort(key=lambda x: x[1], reverse=True)
        total_time = sum(t[1] for t in timings)
        avg_time = total_time / len(timings)
        print(f"Total time:     {total_time*1000:.1f}ms")
        print(f"Average/test:   {avg_time*1000:.1f}ms")
        print(f"Slowest test:   {timings[0][0]} ({timings[0][1]*1000:.1f}ms)")
        print(f"Fastest test:   {timings[-1][0]} ({timings[-1][1]*1000:.1f}ms)")
        print()
        print("Top 5 slowest tests:")
        for name, elapsed in timings[:5]:
            print(f"  {elapsed*1000:8.1f}ms  {name}")

    if failed > 0 or crashed > 0:
        sys.exit(1)
    else:
        print()
        print("All tests passed! 🎉")
        sys.exit(0)


if __name__ == "__main__":
    main()
