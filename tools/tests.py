#!/usr/bin/env python3
"""Test runner for JCC.

Runs all test_*.c files in tests/ directory and reports results.
Supports parallel execution with -j/--jobs.

With --jbc, runs the bytecode round-trip: compile each positive test to
a .jbc file and execute it, exercising the cc_save_bytecode / cc_load_bytecode
FFI-table persistence and the cc_load_libc resolution path.
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


JBC_SKIP_TESTS = {
    "test_ffi_fatal_error.c",
    "test_ffi_type_check_arity.c",
    "test_stack_overflow_large_frame.c",
}

# Tests that hang under leaks --atExit due to fork()/wait() interactions
# with the leaks instrumentation (child inherits MallocStackLogging hooks).
LEAKS_SKIP_TESTS = {
    "test_posix_sys_wait.c",
}


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


def vm_profile_path(profile_dir, test_name, mode):
    if not profile_dir:
        return None
    safe = test_name.replace(os.sep, "__").replace("/", "__")
    return Path(profile_dir) / f"{safe}.{mode}.json"


def run_single_test(idx, test_file, jcc, script_dir, use_leaks, platform, jcc_args,
                    bench=False, jbc_mode=False, profile_dir=None):
    tests_dir = Path(script_dir) / "tests"
    test_name = str(test_file.relative_to(tests_dir))

    is_negative_test = False
    expects_runtime_error = False
    per_test_flags = []
    per_test_run_args = []
    expect_stderr = None
    reject_stderr = None
    try:
        with open(test_file, "r") as f:
            header_lines = [f.readline() for _ in range(5)]
            header = "".join(header_lines)
            if "EXPECT_COMPILE_ERROR" in header:
                is_negative_test = True
            if "EXPECT_RUNTIME_ERROR" in header:
                expects_runtime_error = True
            for line in header_lines:
                if "JCC_FLAGS:" in line:
                    flags_str = line.split("JCC_FLAGS:", 1)[1].strip().rstrip("*/").strip()
                    per_test_flags = flags_str.split()
                if "JCC_RUN_ARGS:" in line:
                    args_str = line.split("JCC_RUN_ARGS:", 1)[1].strip().rstrip("*/").strip()
                    per_test_run_args = args_str.split()
                if "JCC_EXPECT_STDERR:" in line:
                    expect_stderr = line.split("JCC_EXPECT_STDERR:", 1)[1].strip().rstrip("*/").strip()
                if "JCC_REJECT_STDERR:" in line:
                    reject_stderr = line.split("JCC_REJECT_STDERR:", 1)[1].strip().rstrip("*/").strip()
    except Exception:
        pass

    if jbc_mode:
        return run_jbc_roundtrip(
            idx, test_file, test_name, jcc, script_dir, jcc_args, per_test_flags,
            per_test_run_args, is_negative_test, expects_runtime_error, bench,
            profile_dir,
        )

    run_args = ["--", *per_test_run_args] if per_test_run_args else []

    if use_leaks:
        if platform == "macos":
            profile_json = vm_profile_path(profile_dir, test_name, "source")
            profile_args = ["--vm-profile", "--json"] if profile_json else []
            normal_cmd = [
                str(jcc), "-I./include", *jcc_args, *per_test_flags,
                *profile_args, str(test_file), *run_args,
            ]
            normal_result = subprocess.run(
                normal_cmd, capture_output=True, text=True, cwd=script_dir
            )
            skip_leaks = test_file.name in LEAKS_SKIP_TESTS
            if not skip_leaks:
                leak_cmd = [
                    "leaks",
                    "-atExit",
                    "--",
                    str(jcc),
                    "-I./include",
                    *jcc_args,
                    *per_test_flags,
                    *profile_args,
                    str(test_file),
                    *run_args,
                ]
                try:
                    leak_result = subprocess.run(
                        leak_cmd, capture_output=True, text=True, cwd=script_dir,
                        timeout=30,
                    )
                    leak_output = leak_result.stdout + leak_result.stderr
                except subprocess.TimeoutExpired:
                    leak_output = "leaks timed out"
            else:
                leak_output = "0 leaks (skipped)"
            output = (
                normal_result.stdout
                + normal_result.stderr
                + leak_output
            )
            exit_code = normal_result.returncode
            is_leaking = "0 leaks" not in leak_output
            cmd = None
        elif platform == "linux":
            profile_json = vm_profile_path(profile_dir, test_name, "source")
            profile_args = ["--vm-profile", "--json"] if profile_json else []
            cmd = [
                "valgrind",
                "--leak-check=full",
                "--error-exitcode=1",
                "--quiet",
                str(jcc),
                "-I./include",
                *jcc_args,
                *per_test_flags,
                *profile_args,
                str(test_file),
                *run_args,
            ]
        elif platform == "windows":
            profile_json = vm_profile_path(profile_dir, test_name, "source")
            profile_args = ["--vm-profile", "--json"] if profile_json else []
            cmd = [
                "drmemory",
                "-batch",
                "-quiet",
                "--",
                str(jcc),
                "-I./include",
                *jcc_args,
                *per_test_flags,
                *profile_args,
                str(test_file),
                *run_args,
            ]
        else:
            profile_json = vm_profile_path(profile_dir, test_name, "source")
            profile_args = ["--vm-profile", "--json"] if profile_json else []
            cmd = [
                str(jcc), "-I./include", *jcc_args, *per_test_flags,
                *profile_args, str(test_file), *run_args,
            ]
    else:
        profile_json = vm_profile_path(profile_dir, test_name, "source")
        profile_args = ["--vm-profile", "--json"] if profile_json else []
        cmd = [
            str(jcc), "-I./include", *jcc_args, *per_test_flags,
            *profile_args, str(test_file), *run_args,
        ]

    elapsed = None
    if cmd is not None:
        start = time.perf_counter()
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=script_dir)
        elapsed = time.perf_counter() - start
        if profile_json and result.stdout:
            Path(profile_json).write_text(result.stdout)
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
        or "unknown warning option" in output
        or ("expected" in output and "got" in output)
    )

    crashed = exit_code in (134, 139, 136, 141, -6, -11, -8, -13)
    stderr_mismatch = None

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

    if status in ("passed", "negative_pass"):
        if expect_stderr and not re.search(expect_stderr, output, re.MULTILINE):
            status = "stderr_mismatch"
            stderr_mismatch = f"expected stderr to match: {expect_stderr}"
        elif reject_stderr and re.search(reject_stderr, output, re.MULTILINE):
            status = "stderr_mismatch"
            stderr_mismatch = f"expected stderr not to match: {reject_stderr}"

    return {
        "idx": idx,
        "test_name": test_name,
        "exit_code": exit_code,
        "status": status,
        "output": output,
        "is_negative_test": is_negative_test,
        "expects_runtime_error": expects_runtime_error,
        "stderr_mismatch": stderr_mismatch,
        "elapsed": elapsed,
        "vm_profile": str(profile_json) if profile_json else None,
    }


def run_jbc_roundtrip(idx, test_file, test_name, jcc, script_dir, jcc_args,
                      per_test_flags, per_test_run_args, is_negative_test,
                      expects_runtime_error, bench, profile_dir=None):
    """Compile a test to .jbc, then run it. Returns a result dict.

    Skips negative tests (EXPECT_COMPILE_ERROR / EXPECT_RUNTIME_ERROR) and
    tests listed in JBC_SKIP_TESTS, since the round-trip is only meaningful
    for tests that produce a working executable.
    """
    skip_reason = None
    if is_negative_test:
        skip_reason = "negative test"
    elif test_file.name in JBC_SKIP_TESTS:
        skip_reason = "jbc-incompatible"
    if skip_reason:
        return {
            "idx": idx,
            "test_name": test_name,
            "exit_code": 0,
            "status": "jbc_skipped",
            "output": "",
            "is_negative_test": is_negative_test,
            "expects_runtime_error": expects_runtime_error,
            "stderr_mismatch": None,
            "elapsed": None,
            "skip_reason": skip_reason,
            "vm_profile": None,
        }

    with tempfile.TemporaryDirectory() as tmp:
        jbc_path = Path(tmp) / (test_file.stem + ".jbc")
        save_cmd = [
            str(jcc), "-I./include", *jcc_args, *per_test_flags,
            "-c", "-o", str(jbc_path), str(test_file),
        ]
        save = subprocess.run(save_cmd, capture_output=True, text=True, cwd=script_dir)
        if save.returncode != 0:
            return {
                "idx": idx,
                "test_name": test_name,
                "exit_code": save.returncode,
                "status": "jbc_save_failed",
                "output": save.stderr,
                "is_negative_test": False,
                "expects_runtime_error": False,
                "stderr_mismatch": None,
                "elapsed": None,
                "vm_profile": None,
            }

        profile_json = vm_profile_path(profile_dir, test_name, "jbc")
        profile_args = ["--vm-profile", "--json"] if profile_json else []
        run_args = ["--", *per_test_run_args] if per_test_run_args else []
        run_cmd = [str(jcc), *profile_args, str(jbc_path), *run_args]
        start = time.perf_counter() if bench else None
        run = subprocess.run(run_cmd, capture_output=True, text=True, cwd=script_dir)
        elapsed = (time.perf_counter() - start) if bench else None
        output = run.stdout + run.stderr
        if expects_runtime_error and run.returncode == 255:
            status = "jbc_passed"
        elif not expects_runtime_error and run.returncode == 42:
            status = "jbc_passed"
        else:
            status = "jbc_failed"
        return {
            "idx": idx,
            "test_name": test_name,
            "exit_code": run.returncode,
            "status": status,
            "output": output,
            "is_negative_test": False,
            "expects_runtime_error": False,
            "stderr_mismatch": None,
            "elapsed": elapsed,
            "vm_profile": str(profile_json) if profile_json else None,
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
    parser.add_argument(
        "--vm-profile", action="store_true",
        help="Collect per-test VM opcode profile JSON under profile/vm-opcodes"
    )
    parser.add_argument(
        "--jbc", action="store_true",
        help="Run the .jbc bytecode round-trip: compile each positive test to a .jbc, then run it. "
             "Negative tests and a small set of FFI tests that cannot survive rehydration are skipped."
    )
    args, jcc_args = parser.parse_known_args()

    script_dir = Path(__file__).parent.parent.resolve()

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

    if args.jbc:
        if use_leaks:
            print("Warning: --leaks/--profile-mem are not supported in --jbc mode and will be ignored.")
            use_leaks = False
        if args.profile_cpu:
            print("Warning: --profile-cpu is not supported in --jbc mode and will be ignored.")
            args.profile_cpu = False

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
    profile_dir = None
    if args.vm_profile:
        profile_dir = script_dir / "profile" / "vm-opcodes"
        profile_dir.mkdir(parents=True, exist_ok=True)
        print(f"VM opcode profiling enabled (JSON: {profile_dir})")
    if args.match:
        print(f"Filtering tests matching: {args.match}")
    if args.jbc:
        print("JBC mode: compiling each positive test to .jbc, then executing the bytecode")
    print(f"Using {n_jobs} parallel jobs")
    print("=======================")
    print()

    test_args = [
        (
            i, test_file, jcc, str(script_dir), use_leaks, platform, jcc_args,
            args.bench, args.jbc, str(profile_dir) if profile_dir else None,
        )
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
    jbc_passed = 0
    jbc_failed = 0
    jbc_skipped = 0
    jbc_save_failed = 0
    failed_tests = []
    crashed_tests = []
    jbc_skipped_tests = []

    def print_single_result(result):
        nonlocal total, passed, failed, crashed, negative_passed
        nonlocal jbc_passed, jbc_failed, jbc_skipped, jbc_save_failed
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
        elif status == "stderr_mismatch":
            failed += 1
            failed_tests.append(f"{test_name} ({result['stderr_mismatch']})")
            print(f"✗ {test_name} ({result['stderr_mismatch']}){timing_str}")
            for line in output.splitlines()[:5]:
                print(f"  {line}")
        elif status == "jbc_passed":
            jbc_passed += 1
            print(f"✓ {test_name}{timing_str}")
        elif status == "jbc_skipped":
            jbc_skipped += 1
            reason = result.get("skip_reason", "")
            if reason:
                jbc_skipped_tests.append(f"{test_name} ({reason})")
        elif status == "jbc_save_failed":
            jbc_save_failed += 1
            failed += 1
            failed_tests.append(f"{test_name} (JBC SAVE FAILED)")
            print(f"✗ {test_name} (JBC SAVE FAILED){timing_str}")
            for line in output.splitlines()[:3]:
                print(f"  {line}")
        elif status == "jbc_failed":
            jbc_failed += 1
            failed += 1
            failed_tests.append(f"{test_name} (JBC RUNTIME FAILED, exit {exit_code})")
            print(f"✗ {test_name} (JBC RUNTIME FAILED, exit {exit_code}){timing_str}")
            for line in output.splitlines()[:3]:
                print(f"  {line}")

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
                "stderr_mismatch": None,
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
    if args.jbc:
        print(f"Total:          {total}")
        print(f"JBC passed:     {jbc_passed}")
        print(f"JBC skipped:    {jbc_skipped}")
        print(f"JBC failed:     {jbc_failed}")
        print(f"JBC save fail:  {jbc_save_failed}")
    else:
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

    if args.jbc and jbc_skipped > 0:
        print()
        print(f"JBC skipped tests ({jbc_skipped}):")
        for test in jbc_skipped_tests:
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
        if args.jbc:
            print()
            print(f"All {jbc_passed} jbc roundtrips passed ({jbc_skipped} skipped).")
        else:
            print()
            print("All tests passed! 🎉")
        sys.exit(0)


if __name__ == "__main__":
    main()
