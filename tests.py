#!/usr/bin/env python3
"""Test runner for JCC.

Runs all test_*.c files in tests/ directory and reports results.
Supports parallel execution with -j/--jobs.
"""

import argparse
import concurrent.futures
import fnmatch
import os
import re
import subprocess
import sys
import tempfile
import threading
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


def run_single_test(idx, test_file, jcc, script_dir, use_leaks, platform, jcc_args):
    test_name = test_file.name

    is_negative_test = False
    expects_runtime_error = False
    try:
        with open(test_file, "r") as f:
            first_line = f.readline()
            if "EXPECT_COMPILE_ERROR" in first_line:
                is_negative_test = True
            if "EXPECT_RUNTIME_ERROR" in first_line:
                expects_runtime_error = True
    except Exception:
        pass

    if use_leaks:
        if platform == "macos":
            normal_cmd = [str(jcc), "-I./include", *jcc_args, str(test_file)]
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
                str(test_file),
            ]
        else:
            cmd = [str(jcc), "-I./include", *jcc_args, str(test_file)]
    else:
        cmd = [str(jcc), "-I./include", *jcc_args, str(test_file)]

    if cmd is not None:
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=script_dir)
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
    args, jcc_args = parser.parse_known_args()

    script_dir = Path(__file__).parent.resolve()
    jcc = script_dir / "jcc"
    tests_dir = script_dir / "tests"

    if not jcc.exists():
        print("Error: jcc executable not found. Please run 'make' first.")
        sys.exit(1)

    if not tests_dir.exists():
        print("Error: tests directory not found.")
        sys.exit(1)

    platform = detect_platform()

    test_files = sorted(tests_dir.glob("test_*.c"))

    if args.match:
        test_files = [f for f in test_files if fnmatch.fnmatch(f.name, args.match)]

    if not test_files:
        print(f"No test files found in {tests_dir}")
        sys.exit(1)

    if args.jobs is not None:
        n_jobs = args.jobs
    else:
        n_jobs = os.cpu_count() or 1

    use_leaks = args.leaks
    if use_leaks and platform == "unknown":
        print("Warning: Memory leak detection not supported on this platform")
        use_leaks = False

    print("Running JCC tests...")
    if use_leaks:
        leak_tools = {"macos": "leaks", "linux": "valgrind", "windows": "drmemory"}
        print(
            f"Memory leak detection enabled (using '{leak_tools.get(platform, '?')}')"
        )
    if args.match:
        print(f"Filtering tests matching: {args.match}")
    print(f"Using {n_jobs} parallel jobs")
    print("=======================")
    print()

    test_args = [
        (i, test_file, jcc, str(script_dir), use_leaks, platform, jcc_args)
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

        if status == "crashed":
            crashed += 1
            crashed_tests.append(f"{test_name} (exit code: {exit_code})")
            print(f"💥 {test_name} (CRASHED: exit code {exit_code})")
        elif status == "compile_error":
            failed += 1
            failed_tests.append(f"{test_name} (COMPILATION ERROR)")
            print(f"✗ {test_name} (COMPILATION ERROR)")
            for line in output.splitlines()[:3]:
                print(f"  {line}")
        elif status == "leak":
            failed += 1
            failed_tests.append(f"{test_name} (MEMORY LEAK)")
            print(f"💧 {test_name} (MEMORY LEAK)")
            leak_lines = [line for line in output.splitlines() if "Leak:" in line][:3]
            for line in leak_lines:
                print(f"  {line}")
        elif status == "negative_pass":
            negative_passed += 1
            if result["is_negative_test"]:
                print(f"✓ {test_name} (correctly rejected invalid code)")
            else:
                print(f"✓ {test_name} (correctly detected runtime error)")
        elif status == "passed":
            passed += 1
            print(f"✓ {test_name}")
        elif status == "failed":
            failed += 1
            failed_tests.append(f"{test_name} (exit code: {exit_code})")
            print(f"✗ {test_name} (expected exit code 42, got: {exit_code})")

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
                "test_name": test_files[idx].name,
                "exit_code": -1,
                "status": "crashed",
                "output": str(e),
                "is_negative_test": False,
                "expects_runtime_error": False,
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

    def run_extra_regression(name, fn, negative=False):
        try:
            ok, output = fn()
        except Exception as e:
            ok = False
            output = str(e)

        return {
            "idx": len(results),
            "test_name": name,
            "exit_code": 42 if ok else 1,
            "status": "negative_pass"
            if ok and negative
            else ("passed" if ok else "failed"),
            "output": output,
            "is_negative_test": negative,
            "expects_runtime_error": False,
        }

    def hashmap_tombstone_regression():
        src = "\n".join(
            f"#define M{i} {i}\n#undef M{i}" for i in range(1000)
        )
        src += "\nint main(){ return 42; }\n"
        result = subprocess.run(
            [str(jcc), "-"],
            input=src,
            capture_output=True,
            text=True,
            cwd=script_dir,
        )
        return result.returncode == 42, result.stdout + result.stderr

    def unknown_opcode_regression():
        with tempfile.TemporaryDirectory() as tmpdir:
            bc = Path(tmpdir) / "ok.jbc"
            bad = Path(tmpdir) / "bad.jbc"
            src = "int main(){ return 42; }\n"
            saved = subprocess.run(
                [str(jcc), "-o", str(bc), "-"],
                input=src,
                capture_output=True,
                text=True,
                cwd=script_dir,
            )
            if saved.returncode != 0:
                return False, saved.stdout + saved.stderr

            data = bytearray(bc.read_bytes())
            header_size = 4 + 4 + 4 + 8 + 8 + 8 + 8
            first_opcode = header_size + 8
            if first_opcode + 8 > len(data):
                return False, "bytecode file too small"
            data[first_opcode:first_opcode + 8] = (999999).to_bytes(
                8, "little", signed=True
            )
            bad.write_bytes(data)

            loaded = subprocess.run(
                [str(jcc), str(bad)],
                capture_output=True,
                text=True,
                cwd=script_dir,
            )
            output = loaded.stdout + loaded.stderr
            return loaded.returncode != 0 and "unknown opcode" in output, output

    for extra in [
        run_extra_regression(
            "generated_hashmap_tombstones", hashmap_tombstone_regression
        ),
        run_extra_regression(
            "generated_unknown_opcode_bytecode", unknown_opcode_regression
        ),
    ]:
        print_single_result(extra)

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

    if failed > 0 or crashed > 0:
        sys.exit(1)
    else:
        print()
        print("All tests passed! 🎉")
        sys.exit(0)


if __name__ == "__main__":
    main()
