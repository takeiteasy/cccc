#!/usr/bin/env python3
"""Test runner for CCCC.

Runs all test_*.c files in tests/ directory and reports results.
Supports parallel execution with -j/--jobs.

With --c4, runs the bytecode round-trip: compile each positive test to
a .c4 file and execute it, exercising the cc_save_bytecode / cc_load_bytecode
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


C4_SKIP_TESTS = {
    "test_ffi_fatal_error.c",
    "test_ffi_type_check_arity.c",
    "test_stack_overflow_large_frame.c",
}

# Tests that hang under leaks --atExit due to fork()/wait() interactions
# with the leaks instrumentation (child inherits MallocStackLogging hooks).
# The child inherits the leak-tracking library injected by leaks -atExit,
# so if the test calls exit() or triggers a signal, the library's atexit
# handler in the child hangs.
LEAKS_SKIP_TESTS = {
    "test_posix_sys_wait.c",
    "test_exit_code.c",
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


def run_single_test(idx, test_file, cccc, script_dir, use_leaks, platform, cccc_args,
                    bench=False, c4_mode=False, profile_dir=None, process_timeout=None):
    tests_dir = Path(script_dir) / "tests"
    test_name = str(test_file.relative_to(tests_dir))

    is_negative_test = False
    expects_runtime_error = False
    is_testing_mode = False
    is_build_mode = False
    per_test_flags = []
    per_test_run_args = []
    expect_stderr = None
    reject_stderr = None
    expect_stdout = None
    reject_stdout = None
    stdout = ""
    stderr = ""
    try:
        with open(test_file, "r") as f:
            header_lines = [f.readline() for _ in range(5)]
            header = "".join(header_lines)
            if "EXPECT_COMPILE_ERROR" in header:
                is_negative_test = True
            if "EXPECT_RUNTIME_ERROR" in header:
                expects_runtime_error = True
            c4_skip = False
            for line in header_lines:
                if "CCCC_C4_SKIP" in line:
                    c4_skip = True
                if "CCCC_FLAGS:" in line:
                    flags_str = line.split("CCCC_FLAGS:", 1)[1].strip().rstrip("*/").strip()
                    per_test_flags = flags_str.split()
                    if "--testing" in per_test_flags:
                        is_testing_mode = True
                    if "--build" in per_test_flags:
                        is_build_mode = True
                if "CCCC_RUN_ARGS:" in line:
                    args_str = line.split("CCCC_RUN_ARGS:", 1)[1].strip().rstrip("*/").strip()
                    per_test_run_args = args_str.split()
                if "CCCC_EXPECT_STDERR:" in line:
                    expect_stderr = line.split("CCCC_EXPECT_STDERR:", 1)[1].strip().rstrip("*/").strip()
                if "CCCC_REJECT_STDERR:" in line:
                    reject_stderr = line.split("CCCC_REJECT_STDERR:", 1)[1].strip().rstrip("*/").strip()
                if "CCCC_EXPECT_STDOUT:" in line:
                    expect_stdout = line.split("CCCC_EXPECT_STDOUT:", 1)[1].strip().rstrip("*/").strip()
                if "CCCC_REJECT_STDOUT:" in line:
                    reject_stdout = line.split("CCCC_REJECT_STDOUT:", 1)[1].strip().rstrip("*/").strip()
    except Exception:
        pass

    if c4_mode and c4_skip:
        return {
            "idx": idx,
            "test_name": test_name,
            "exit_code": 0,
            "status": "c4_skipped",
            "output": "",
            "is_negative_test": is_negative_test,
            "expects_runtime_error": expects_runtime_error,
            "stderr_mismatch": None,
            "elapsed": 0,
            "skip_reason": "c4-incompatible: CCCC_C4_SKIP",
        }

    if c4_mode and is_build_mode:
        # Build scripts emit no bytecode to round-trip; the runner compiles
        # native targets instead. Skip the .c4 pass entirely.
        return {
            "idx": idx,
            "test_name": test_name,
            "exit_code": 0,
            "status": "c4_skipped",
            "output": "",
            "is_negative_test": is_negative_test,
            "expects_runtime_error": expects_runtime_error,
            "stderr_mismatch": None,
            "elapsed": 0,
            "skip_reason": "c4-incompatible: --build mode",
        }

    if c4_mode:
        return run_c4_roundtrip(
            idx, test_file, test_name, cccc, script_dir, cccc_args, per_test_flags,
            per_test_run_args, is_negative_test, expects_runtime_error, bench,
            profile_dir,
            is_testing_mode=is_testing_mode,
            expect_stdout=expect_stdout,
            reject_stdout=reject_stdout,
            expect_stderr=expect_stderr,
            reject_stderr=reject_stderr,
            process_timeout=process_timeout,
        )

    run_args = ["--", *per_test_run_args] if per_test_run_args else []

    if use_leaks:
        if platform == "macos":
            profile_json = vm_profile_path(profile_dir, test_name, "source")
            profile_args = ["--vm-profile", "--json"] if profile_json else []
            normal_cmd = [
                str(cccc), "-I./include", *cccc_args, *per_test_flags,
                *profile_args, str(test_file), *run_args,
            ]
            try:
                normal_result = subprocess.run(
                    normal_cmd, capture_output=True, text=True, cwd=script_dir,
                    timeout=process_timeout,
                )
            except subprocess.TimeoutExpired:
                return {
                    "idx": idx,
                    "test_name": test_name,
                    "exit_code": -1,
                    "status": "timeout",
                    "output": "",
                    "is_negative_test": is_negative_test,
                    "expects_runtime_error": expects_runtime_error,
                    "stderr_mismatch": None,
                    "elapsed": process_timeout,
                    "vm_profile": str(profile_json) if profile_json else None,
                }
            skip_leaks = test_file.name in LEAKS_SKIP_TESTS
            if not skip_leaks:
                leak_cmd = [
                    "leaks",
                    "-atExit",
                    "--",
                    str(cccc),
                    "-V",
                    "-I./include",
                    *cccc_args,
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
            stdout = normal_result.stdout
            stderr = normal_result.stderr
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
                str(cccc),
                "-V",
                "-I./include",
                *cccc_args,
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
                str(cccc),
                "-V",
                "-I./include",
                *cccc_args,
                *per_test_flags,
                *profile_args,
                str(test_file),
                *run_args,
            ]
        else:
            profile_json = vm_profile_path(profile_dir, test_name, "source")
            profile_args = ["--vm-profile", "--json"] if profile_json else []
            cmd = [
                str(cccc), "-I./include", *cccc_args, *per_test_flags,
                *profile_args, str(test_file), *run_args,
            ]
    else:
        profile_json = vm_profile_path(profile_dir, test_name, "source")
        profile_args = ["--vm-profile", "--json"] if profile_json else []
        cmd = [
            str(cccc), "-I./include", *cccc_args, *per_test_flags,
            *profile_args, str(test_file), *run_args,
        ]

    elapsed = None
    if cmd is not None:
        start = time.perf_counter()
        try:
            result = subprocess.run(
                cmd, capture_output=True, text=True, cwd=script_dir,
                timeout=process_timeout,
            )
        except subprocess.TimeoutExpired:
            return {
                "idx": idx,
                "test_name": test_name,
                "exit_code": -1,
                "status": "timeout",
                "output": "",
                "is_negative_test": is_negative_test,
                "expects_runtime_error": expects_runtime_error,
                "stderr_mismatch": None,
                "elapsed": time.perf_counter() - start,
                "vm_profile": str(profile_json) if profile_json else None,
            }
        elapsed = time.perf_counter() - start
        if profile_json and result.stdout:
            Path(profile_json).write_text(result.stdout)
        stdout = result.stdout
        stderr = result.stderr
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
    elif is_testing_mode and exit_code == 0:
        status = "passed"
    elif expect_stdout and exit_code == 0:
        status = "passed"
    elif expects_runtime_error and exit_code == 255:
        status = "negative_pass"
    elif is_build_mode and expect_stderr and exit_code != 0:
        # A --build script CLI/resolution error (e.g. main() defined, ambiguous
        # entry). The expect_stderr regex below validates the diagnostic.
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
        elif expect_stdout and not re.search(expect_stdout, stdout, re.MULTILINE | re.DOTALL):
            status = "stderr_mismatch"
            stderr_mismatch = f"expected stdout to match: {expect_stdout}"
        elif reject_stdout and re.search(reject_stdout, stdout, re.MULTILINE | re.DOTALL):
            status = "stderr_mismatch"
            stderr_mismatch = f"expected stdout not to match: {reject_stdout}"

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


def run_c4_roundtrip(idx, test_file, test_name, cccc, script_dir, cccc_args,
                     per_test_flags, per_test_run_args, is_negative_test,
                     expects_runtime_error, bench, profile_dir=None,
                     is_testing_mode=False, expect_stdout=None,
                     reject_stdout=None, expect_stderr=None, reject_stderr=None,
                     process_timeout=None):
    """Compile a test to .c4, then run it. Returns a result dict.

    Negative tests (EXPECT_COMPILE_ERROR) are run through the compile step
    only: a non-zero exit code counts as c4_passed (expected failure), zero
    counts as c4_failed. They are never skipped on account of being negative.

    Skips tests listed in C4_SKIP_TESTS and mode-incompatible tests (--testing
    prepass, preprocess-only, macro/emit output, or compile-time diagnostic
    tests whose pass condition depends on stderr rather than exit code), but
    only when those tests are not negative tests.
    """
    skip_reason = None
    if not is_negative_test and test_file.name in C4_SKIP_TESTS:
        skip_reason = "c4-incompatible"
    elif not is_negative_test and is_testing_mode:
        skip_reason = "c4-incompatible: testing prepass"
    elif not is_negative_test and "-E" in per_test_flags:
        skip_reason = "c4-incompatible: preprocess-only output"
    elif not is_negative_test and ("-M" in per_test_flags or "-G" in per_test_flags):
        skip_reason = "c4-incompatible: macro/emit generated source"
    elif not is_negative_test and (expect_stderr is not None or reject_stderr is not None):
        skip_reason = "c4-incompatible: compile-time diagnostic"
    if skip_reason:
        return {
            "idx": idx,
            "test_name": test_name,
            "exit_code": 0,
            "status": "c4_skipped",
            "output": "",
            "is_negative_test": is_negative_test,
            "expects_runtime_error": expects_runtime_error,
            "stderr_mismatch": None,
            "elapsed": None,
            "skip_reason": skip_reason,
            "vm_profile": None,
        }

    with tempfile.TemporaryDirectory() as tmp:
        c4_path = Path(tmp) / (test_file.stem + ".c4")
        save_cmd = [
            str(cccc), "-I./include", *cccc_args, *per_test_flags,
            "-c", "-o", str(c4_path), str(test_file),
        ]
        try:
            save = subprocess.run(
                save_cmd, capture_output=True, text=True, cwd=script_dir,
                timeout=process_timeout,
            )
        except subprocess.TimeoutExpired:
            return {
                "idx": idx,
                "test_name": test_name,
                "exit_code": -1,
                "status": "c4_save_failed",
                "output": "TIMEOUT",
                "is_negative_test": False,
                "expects_runtime_error": False,
                "stderr_mismatch": None,
                "elapsed": None,
                "vm_profile": None,
            }
        if is_negative_test:
            status = "c4_passed" if save.returncode != 0 else "c4_failed"
            return {
                "idx": idx,
                "test_name": test_name,
                "exit_code": save.returncode,
                "status": status,
                "output": save.stderr,
                "is_negative_test": True,
                "expects_runtime_error": False,
                "stderr_mismatch": None,
                "elapsed": None,
                "vm_profile": None,
            }
        if save.returncode != 0:
            return {
                "idx": idx,
                "test_name": test_name,
                "exit_code": save.returncode,
                "status": "c4_save_failed",
                "output": save.stderr,
                "is_negative_test": False,
                "expects_runtime_error": False,
                "stderr_mismatch": None,
                "elapsed": None,
                "vm_profile": None,
            }

        profile_json = vm_profile_path(profile_dir, test_name, "c4")
        profile_args = ["--vm-profile", "--json"] if profile_json else []
        run_args = ["--", *per_test_run_args] if per_test_run_args else []
        run_cmd = [str(cccc), *profile_args, str(c4_path), *run_args]
        start = time.perf_counter() if bench else None
        try:
            run = subprocess.run(
                run_cmd, capture_output=True, text=True, cwd=script_dir,
                timeout=process_timeout,
            )
        except subprocess.TimeoutExpired:
            return {
                "idx": idx,
                "test_name": test_name,
                "exit_code": -1,
                "status": "c4_failed",
                "output": "TIMEOUT",
                "is_negative_test": False,
                "expects_runtime_error": False,
                "stderr_mismatch": None,
                "elapsed": (time.perf_counter() - start) if start else None,
                "vm_profile": str(profile_json) if profile_json else None,
            }
        elapsed = (time.perf_counter() - start) if bench else None
        stdout = run.stdout
        output = run.stdout + run.stderr
        if expects_runtime_error and run.returncode == 255:
            status = "c4_passed"
        elif not expects_runtime_error and run.returncode == 42:
            status = "c4_passed"
        elif expect_stdout is not None and run.returncode == 0:
            # Tests that validate via stdout output (not exit 42)
            if re.search(expect_stdout, stdout, re.MULTILINE | re.DOTALL):
                status = "c4_passed"
            else:
                status = "c4_failed"
        else:
            status = "c4_failed"
        # Apply reject_stdout to passing tests
        if status == "c4_passed" and reject_stdout is not None:
            if re.search(reject_stdout, stdout, re.MULTILINE | re.DOTALL):
                status = "c4_failed"
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


def _run_test_suite(cccc, script_dir, use_leaks, platform, cccc_args, n_jobs, args, test_files, header=None):
    """Execute test files and return aggregate results dict."""
    if header:
        print(header)

    tests_dir = script_dir / "tests"

    profile_dir = None
    if args.vm_profile:
        profile_dir = script_dir / "profile" / "vm-opcodes"
        profile_dir.mkdir(parents=True, exist_ok=True)

    process_timeout = getattr(args, "process_timeout", None)
    test_args = [
        (
            i, test_file, cccc, str(script_dir), use_leaks, platform, cccc_args,
            args.bench, args.c4, str(profile_dir) if profile_dir else None,
            process_timeout,
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
    c4_passed = 0
    c4_failed = 0
    c4_skipped = 0
    c4_save_failed = 0
    failed_tests = []
    crashed_tests = []
    c4_skipped_tests = []
    timings = []

    def print_single_result(result):
        nonlocal total, passed, failed, crashed, negative_passed
        nonlocal c4_passed, c4_failed, c4_skipped, c4_save_failed
        total += 1
        test_name = result["test_name"]
        status = result["status"]
        exit_code = result["exit_code"]
        output = result["output"]
        elapsed = result.get("elapsed")
        quiet = args.quiet

        timing_str = ""
        if args.bench and elapsed is not None:
            timing_str = f" [{elapsed*1000:.1f}ms]"
            timings.append((test_name, elapsed))

        if status == "crashed":
            crashed += 1
            crashed_tests.append(f"{test_name} (exit code: {exit_code})")
            if not quiet:
                print(f"💥 {test_name} (CRASHED: exit code {exit_code}){timing_str}")
        elif status == "compile_error":
            failed += 1
            failed_tests.append(f"{test_name} (COMPILATION ERROR)")
            if not quiet:
                print(f"✗ {test_name} (COMPILATION ERROR){timing_str}")
                for line in output.splitlines()[:3]:
                    print(f"  {line}")
        elif status == "leak":
            failed += 1
            failed_tests.append(f"{test_name} (MEMORY LEAK)")
            if not quiet:
                print(f"💧 {test_name} (MEMORY LEAK){timing_str}")
                leak_lines = [line for line in output.splitlines() if "Leak:" in line][:3]
                for line in leak_lines:
                    print(f"  {line}")
        elif status == "negative_pass":
            negative_passed += 1
            if not quiet:
                if result["is_negative_test"]:
                    print(f"✓ {test_name} (correctly rejected invalid code){timing_str}")
                else:
                    print(f"✓ {test_name} (correctly detected runtime error){timing_str}")
        elif status == "passed":
            passed += 1
            if not quiet:
                print(f"✓ {test_name}{timing_str}")
        elif status == "timeout":
            failed += 1
            failed_tests.append(f"{test_name} (TIMEOUT)")
            if not quiet:
                print(f"✗ {test_name} (TIMEOUT){timing_str}")
        elif status == "failed":
            failed += 1
            failed_tests.append(f"{test_name} (exit code: {exit_code})")
            if not quiet:
                print(f"✗ {test_name} (expected exit code 42, got: {exit_code}){timing_str}")
        elif status == "stderr_mismatch":
            failed += 1
            failed_tests.append(f"{test_name} ({result['stderr_mismatch']})")
            if not quiet:
                print(f"✗ {test_name} ({result['stderr_mismatch']}){timing_str}")
                for line in output.splitlines()[:5]:
                    print(f"  {line}")
        elif status == "c4_passed":
            c4_passed += 1
            if not quiet:
                print(f"✓ {test_name}{timing_str}")
        elif status == "c4_skipped":
            c4_skipped += 1
            reason = result.get("skip_reason", "")
            if reason:
                c4_skipped_tests.append(f"{test_name} ({reason})")
        elif status == "c4_save_failed":
            c4_save_failed += 1
            failed += 1
            failed_tests.append(f"{test_name} (C4 SAVE FAILED)")
            if not quiet:
                print(f"✗ {test_name} (C4 SAVE FAILED){timing_str}")
                for line in output.splitlines()[:3]:
                    print(f"  {line}")
        elif status == "c4_failed":
            c4_failed += 1
            failed += 1
            failed_tests.append(f"{test_name} (C4 RUNTIME FAILED, exit {exit_code})")
            if not quiet:
                print(f"✗ {test_name} (C4 RUNTIME FAILED, exit {exit_code}){timing_str}")
                for line in output.splitlines()[:3]:
                    print(f"  {line}")

    def flush_results():
        nonlocal next_to_print
        while next_to_print < len(results) and results[next_to_print] is not None:
            print_single_result(results[next_to_print])
            next_to_print += 1

    def on_done(future, idx):
        try:
            result = future.result()
        except Exception as e:
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

    return {
        "total": total,
        "passed": passed,
        "failed": failed,
        "crashed": crashed,
        "negative_passed": negative_passed,
        "c4_passed": c4_passed,
        "c4_failed": c4_failed,
        "c4_skipped": c4_skipped,
        "c4_save_failed": c4_save_failed,
        "failed_tests": failed_tests,
        "crashed_tests": crashed_tests,
        "c4_skipped_tests": c4_skipped_tests,
        "timings": timings,
    }


def main():
    parser = argparse.ArgumentParser(description="Test runner for CCCC")
    parser.add_argument(
        "--full", action="store_true",
        help="Run test suite with each optimization level (-O0..-O4) and show a combined summary"
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
        "--profile-cpu", action="store_true", help="Run tests under gperftools CPU profiler (builds cccc-prof if needed)"
    )
    parser.add_argument(
        "--profile-mem", action="store_true", help="Run tests with enhanced memory profiling (macOS: leaks+heap, Linux: valgrind)"
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
    args, cccc_args = parser.parse_known_args()

    script_dir = Path(__file__).parent.parent.resolve()

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

    profile_dir = None
    if args.vm_profile:
        profile_dir = script_dir / "profile" / "vm-opcodes"
        profile_dir.mkdir(parents=True, exist_ok=True)

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
        if args.vm_profile:
            print(f"VM opcode profiling enabled (JSON: {profile_dir})")
        if args.match:
            print(f"Filtering tests matching: {args.match}")
        if args.c4:
            print("C4 mode: compiling each positive test to .c4, then executing the bytecode")
        print(f"Using {n_jobs} parallel jobs")
        print("=======================")
        print()

    if args.full:
        # Strip any existing -O/--optimize flags from cccc_args
        filtered = []
        skip = False
        for a in cccc_args:
            if skip:
                skip = False
                continue
            if a in ("-O", "--optimize"):
                skip = True
                continue
            if a.startswith("-O") and len(a) > 2 and a[2].isdigit():
                continue
            if a.startswith("--optimize="):
                continue
            filtered.append(a)

        levels = [
            (0, "none"),
            (1, "basic"),
            (2, "standard"),
            (3, "aggressive"),
            (4, "fused"),
        ]
        all_results = {}
        for level, name in levels:
            level_args = filtered + [f"-O{level}"]
            if not args.quiet:
                print()
                print(f"--- Optimization Level {level} ({name}) ---")
                print()
            all_results[level] = _run_test_suite(
                cccc, script_dir, use_leaks, platform, level_args,
                n_jobs, args, test_files,
            )

        print()
        print("=====================================")
        print("Full Optimization Suite Summary")
        print("=====================================")
        print(f"{'Level':<18} {'Total':>6} {'Passed':>6} {'Failed':>6} {'Crashed':>6}")
        print("-" * 48)
        grand_total = 0
        grand_passed = 0
        grand_failed = 0
        grand_crashed = 0
        for level, name in levels:
            r = all_results[level]
            r_total = r["total"]
            r_passed = r["passed"] + r["negative_passed"] + r["c4_passed"]
            r_failed = r["failed"]
            r_crashed = r["crashed"]
            label = f"-O{level} ({name})"
            print(f"{label:<18} {r_total:>6} {r_passed:>6} {r_failed:>6} {r_crashed:>6}")
            grand_total += r_total
            grand_passed += r_passed
            grand_failed += r_failed
            grand_crashed += r_crashed
        print("-" * 48)
        print(f"{'Sum':<18} {grand_total:>6} {grand_passed:>6} {grand_failed:>6} {grand_crashed:>6}")
        print()

        # Collect per-test failures by level
        per_test_levels = {}
        for level, name in levels:
            r = all_results[level]
            label = f"-O{level}"
            for entry in r["failed_tests"] + r["crashed_tests"]:
                test_name = entry.split(" (")[0]
                per_test_levels.setdefault(test_name, set()).add(label)

        if per_test_levels:
            print("Failed Tests by Level:")
            print("-" * 48)
            all_level_labels = {f"-O{l}" for l, _ in levels}
            for test_name in sorted(per_test_levels):
                levels_set = per_test_levels[test_name]
                if levels_set == all_level_labels:
                    level_str = "all levels"
                else:
                    level_str = ", ".join(sorted(levels_set))
                print(f"  ✗ {test_name}  ({level_str})")
            print()

        if grand_failed > 0 or grand_crashed > 0:
            sys.exit(1)
        print("All levels passed!")
        sys.exit(0)

    r = _run_test_suite(
        cccc, script_dir, use_leaks, platform, cccc_args,
        n_jobs, args, test_files,
    )
    total = r["total"]
    passed = r["passed"]
    failed = r["failed"]
    crashed = r["crashed"]
    negative_passed = r["negative_passed"]
    c4_passed = r["c4_passed"]
    c4_failed = r["c4_failed"]
    c4_skipped = r["c4_skipped"]
    c4_save_failed = r["c4_save_failed"]
    failed_tests = r["failed_tests"]
    crashed_tests = r["crashed_tests"]
    c4_skipped_tests = r["c4_skipped_tests"]
    timings = r["timings"]

    print()
    print("=======================")
    print("Test Results Summary")
    print("=======================")
    if args.c4:
        print(f"Total:          {total}")
        print(f"C4 passed:      {c4_passed}")
        print(f"C4 skipped:     {c4_skipped}")
        print(f"C4 failed:      {c4_failed}")
        print(f"C4 save fail:   {c4_save_failed}")
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

    if args.c4 and c4_skipped > 0 and not args.quiet:
        print()
        print(f"C4 skipped tests ({c4_skipped}):")
        for test in c4_skipped_tests:
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
        if args.c4:
            print()
            print(f"All {c4_passed} c4 roundtrips passed ({c4_skipped} skipped).")
        else:
            print()
            print("All tests passed! 🎉")
        sys.exit(0)


if __name__ == "__main__":
    main()
