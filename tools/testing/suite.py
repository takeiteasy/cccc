"""Parallel test suite executor."""

import concurrent.futures
import threading
from pathlib import Path

from .runner import run_single_test


def _run_test_suite(cccc, script_dir, use_leaks, platform, cccc_args, n_jobs, args,
                    test_files, header=None, matrix_mode=False):
    """Execute test files in parallel and return aggregate results dict.

    matrix_mode: passed through to run_single_test to strip per-test -On flags.
    """
    if header:
        print(header)

    tests_dir = script_dir / "tests"

    profile_dir = None
    if getattr(args, "vm_profile", False):
        profile_dir = script_dir / "profile" / "vm-opcodes"
        profile_dir.mkdir(parents=True, exist_ok=True)

    process_timeout = getattr(args, "process_timeout", None)
    test_args = [
        (
            i, test_file, cccc, str(script_dir), use_leaks, platform, cccc_args,
            getattr(args, "bench", False),
            getattr(args, "c4", False),
            str(profile_dir) if profile_dir else None,
            process_timeout,
            matrix_mode,
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
    matrix_skipped = 0
    failed_tests = []
    crashed_tests = []
    c4_skipped_tests = []
    matrix_skipped_tests = []
    timings = []

    quiet = getattr(args, "quiet", False)

    def print_single_result(result):
        nonlocal total, passed, failed, crashed, negative_passed
        nonlocal c4_passed, c4_failed, c4_skipped, c4_save_failed, matrix_skipped
        total += 1
        test_name = result["test_name"]
        status = result["status"]
        exit_code = result["exit_code"]
        output = result["output"]
        elapsed = result.get("elapsed")

        timing_str = ""
        if getattr(args, "bench", False) and elapsed is not None:
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
        elif status == "matrix_skipped":
            matrix_skipped += 1
            reason = result.get("skip_reason", "")
            if reason:
                matrix_skipped_tests.append(f"{test_name} ({reason})")
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
        "matrix_skipped": matrix_skipped,
        "failed_tests": failed_tests,
        "crashed_tests": crashed_tests,
        "c4_skipped_tests": c4_skipped_tests,
        "matrix_skipped_tests": matrix_skipped_tests,
        "timings": timings,
    }
