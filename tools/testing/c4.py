"""Bytecode round-trip (.c4) test execution."""

import re
import subprocess
import tempfile
import time
from pathlib import Path

from . import C4_SKIP_TESTS, vm_profile_path


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
        # --test-c4 handles the round-trip internally (compile → save .c4 → reload → run).
        # Run as a single invocation; per_test_flags already contains "--testing".
        cmd = [str(cccc), "-I./include", *cccc_args, *per_test_flags,   # noqa: E501
               "--test-c4", str(test_file)]
        start = time.perf_counter() if bench else None
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
                "status": "c4_failed",
                "output": "TIMEOUT",
                "is_negative_test": False,
                "expects_runtime_error": False,
                "stderr_mismatch": None,
                "elapsed": (time.perf_counter() - start) if start else None,
                "vm_profile": None,
            }
        elapsed = (time.perf_counter() - start) if bench else None
        status = "c4_passed" if result.returncode == 0 else "c4_failed"
        return {
            "idx": idx,
            "test_name": test_name,
            "exit_code": result.returncode,
            "status": status,
            "output": result.stdout + result.stderr,
            "is_negative_test": False,
            "expects_runtime_error": False,
            "stderr_mismatch": None,
            "elapsed": elapsed,
            "vm_profile": None,
        }
    elif not is_negative_test and "-E" in per_test_flags:
        skip_reason = "c4-incompatible: preprocess-only output"
    elif not is_negative_test and (
        "-m" in per_test_flags or
        any(f in ("-c=generated", "-c=gen", "-c=g", "-cgenerated", "-cgen",
                   "-cg", "--compile=generated", "--compile=gen",
                   "--compile=g")
            for f in per_test_flags)
    ):
        # #924/#936: this checked "-M" (--memory-leak-detection) instead of
        # "-m" (--dump-expanded) -- a real safety flag, not the
        # serialize-and-exit mode this skip exists for. A test combining
        # "-m" (or the folded-in "-c=generated", formerly "-G") with the c4
        # round-trip used to slip through: -c/-o here still saves to
        # c4_path, but -m's/-c=generated's early exit in main.c serializes C
        # source into that path instead of bytecode, so the reload step
        # below failed with "failed to load bytecode" -- a harness bug, not
        # a real regression in the test under round-trip.
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
            "-c=bytecode", "-o", str(c4_path), str(test_file),
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
        # Re-apply per_test_flags on the run step too, not just the save
        # step above: runtime-checked flags like --ffi-type-checking take
        # effect in the VM at execution time, not compile time, so a saved
        # .c4 re-run without them silently skips that check instead of
        # exercising it (#883). Compile-only flags (-O2, --std=, -Wall, ...)
        # are harmless no-ops when passed alongside a .c4 file to execute
        # directly -- verified they don't error or change behavior.
        run_cmd = [str(cccc), *profile_args, *per_test_flags, str(c4_path), *run_args]
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
