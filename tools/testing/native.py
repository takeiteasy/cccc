"""Native-backend (-c=native) round-trip test execution.

Mirrors c4.py's shape, with one structural difference: under --c4, cccc
itself executes the bytecode and returns 42 (c4.py's run step). Under
--native, cccc's own exit code only reports whether the *compile* succeeded
(0) or failed (nonzero) -- the compiled *child* binary is a second process,
and it is the child that is expected to return 42. -I./include and the test's
CCCC_FLAGS apply to the compile step only; CCCC_RUN_ARGS go to the child.

Three tiers, chosen by run_single_test (runner.py) before calling in here:
  - SKIP: filtered out entirely (see NATIVE_SKIP_TESTS / NATIVE_VM_ONLY_FLAGS
    in __init__.py, plus the mode/flag checks in native_skip_reason below).
  - COMPILE-ONLY: EXPECT_COMPILE_ERROR (must fail to compile),
    EXPECT_RUNTIME_ERROR (exit 255 is a VM-only convention -- #935 -- so this
    only asserts the compile succeeds), and CCCC_EXPECT_STDERR/REJECT_STDERR
    tests (the diagnostic assertion is already covered by the normal VM run;
    here we only assert -c=native didn't regress into a build failure).
  - FULL: compile, confirm the artifact exists, run it, check exit 42 (or
    CCCC_EXPECT_STDOUT/REJECT_STDOUT against the child's stdout).
"""

import re
import subprocess
import tempfile
import time
from pathlib import Path

from . import native_skip_reason


def _skip_result(idx, test_name, reason, is_negative_test, expects_runtime_error):
    return {
        "idx": idx,
        "test_name": test_name,
        "exit_code": 0,
        "status": "native_skipped",
        "output": "",
        "is_negative_test": is_negative_test,
        "expects_runtime_error": expects_runtime_error,
        "stderr_mismatch": None,
        "elapsed": None,
        "skip_reason": reason,
        "vm_profile": None,
    }


def _run_testing_suite(idx, test_file, test_name, cccc, script_dir, cccc_args,
                       per_test_flags, bench, process_timeout):
    """#1033: compile+run a [[cccc::test]] suite file through the generated
    native harness. `--testing` in per_test_flags is replaced with
    `--testing=native` (bare `--testing` alone means the VM backend, see
    src/main.c); `-O<n>`/`-f<pass>` are stripped like the single-file path
    (native.py's own docstring), same reasoning.

    The compiled artifact is a self-contained TAP runner (its own main()) --
    pass/fail is its exit code, matching cc_run_tests's own `passed == n`
    convention, not the exit-42 single-test convention.
    """
    testing_flags = [
        f for f in per_test_flags
        if f != "--testing"
        and not (f.startswith("-O") and len(f) > 2 and f[2].isdigit())
        and f not in ("-O", "--optimize")
        and not f.startswith("--optimize=")
        and not f.startswith("-f")
    ]

    with tempfile.TemporaryDirectory() as tmp:
        out_path = Path(tmp) / (test_file.stem + "_native_out")
        compile_cmd = [
            str(cccc), "-I./include", *cccc_args, *testing_flags,
            "--testing=native", "-o", str(out_path), str(test_file),
        ]
        start = time.perf_counter() if bench else None
        try:
            compiled = subprocess.run(
                compile_cmd, capture_output=True, text=True, cwd=script_dir,
                timeout=process_timeout,
            )
        except subprocess.TimeoutExpired:
            return {
                "idx": idx, "test_name": test_name, "exit_code": -1,
                "status": "native_compile_failed", "output": "TIMEOUT",
                "is_negative_test": False, "expects_runtime_error": False,
                "stderr_mismatch": None,
                "elapsed": (time.perf_counter() - start) if start else None,
                "vm_profile": None,
            }

        artifact_ok = compiled.returncode == 0 and out_path.exists()
        if not artifact_ok:
            return {
                "idx": idx, "test_name": test_name,
                "exit_code": compiled.returncode,
                "status": "native_compile_failed",
                "output": compiled.stderr or compiled.stdout,
                "is_negative_test": False, "expects_runtime_error": False,
                "stderr_mismatch": None, "elapsed": None, "vm_profile": None,
            }

        try:
            run = subprocess.run(
                [str(out_path)], capture_output=True, text=True, cwd=script_dir,
                timeout=process_timeout,
            )
        except subprocess.TimeoutExpired:
            return {
                "idx": idx, "test_name": test_name, "exit_code": -1,
                "status": "native_failed", "output": "TIMEOUT",
                "is_negative_test": False, "expects_runtime_error": False,
                "stderr_mismatch": None,
                "elapsed": (time.perf_counter() - start) if start else None,
                "vm_profile": None,
            }
        elapsed = (time.perf_counter() - start) if bench else None
        status = "native_passed" if run.returncode == 0 else "native_failed"
        return {
            "idx": idx, "test_name": test_name, "exit_code": run.returncode,
            "status": status, "output": run.stdout + run.stderr,
            "is_negative_test": False, "expects_runtime_error": False,
            "stderr_mismatch": None, "elapsed": elapsed, "vm_profile": None,
        }


def run_native_roundtrip(idx, test_file, test_name, cccc, script_dir, cccc_args,
                         per_test_flags, per_test_run_args, is_negative_test,
                         expects_runtime_error, bench, is_diagnostic_test=False,
                         expect_stdout=None, reject_stdout=None,
                         process_timeout=None, platform=None,
                         is_testing_mode=False):
    """Compile a test with -c=native, then (for FULL-tier tests) run the
    resulting binary. Returns a result dict shaped like c4.run_c4_roundtrip's.

    is_negative_test:      EXPECT_COMPILE_ERROR -- compile must fail.
    expects_runtime_error: EXPECT_RUNTIME_ERROR -- compile-only (see module
                            docstring); the exit-255 VM convention is not
                            asserted natively.
    is_diagnostic_test:    CCCC_EXPECT_STDERR/CCCC_REJECT_STDERR present --
                            compile-only, same reasoning.
    platform:               "macos"/"linux"/"windows" (runner.py's own
                            detection) -- forwarded to native_skip_reason so
                            a platform-only gap (e.g. #1028) skips just there.
    is_testing_mode:        CCCC_FLAGS carries --testing -- this is a
                            [[cccc::test]] suite file, not a single-file
                            EXPECT-style test. Routed to _run_testing_suite
                            below (#1033): compile with --testing=native
                            (which implies -c=native and serializes the
                            harness itself) instead of the exit-42 tier
                            logic, since the compiled artifact is its own
                            TAP-emitting test runner, not a single program
                            whose own exit code is the verdict.
    """
    reason = native_skip_reason(test_file.name, per_test_flags, cccc_args, platform)
    if reason:
        return _skip_result(idx, test_name, reason, is_negative_test, expects_runtime_error)

    # A negative/EXPECT_RUNTIME_ERROR/diagnostic-only test that also
    # happens to carry --testing in its CCCC_FLAGS (a handful of legacy
    # single-file tests exercising the --testing frontend itself, not a
    # [[cccc::test]] suite corpus file) already has correct handling
    # further below -- generic compile-fail/stderr-match, same as any
    # other negative test. Only a genuinely positive testing-mode file
    # routes to the generated suite harness.
    if is_testing_mode and not (is_negative_test or expects_runtime_error
                                or is_diagnostic_test):
        return _run_testing_suite(idx, test_file, test_name, cccc, script_dir,
                                  cccc_args, per_test_flags, bench, process_timeout)

    # -O<n>/--optimize=<n>/-f<pass> tune the VM bytecode pipeline; -c=native
    # rejects them outright ("cannot be combined with VM bytecode options",
    # main.c's opt_level/opt_f_enable/opt_f_disable check). Strip them rather
    # than skip, mirroring runner.py's is_build_mode handling (runner.py:155-158)
    # and matrix.py's own -O/--optimize stripping (matrix.py:48-63).
    native_flags = [
        f for f in per_test_flags
        if not (f.startswith("-O") and len(f) > 2 and f[2].isdigit())
        and f not in ("-O", "--optimize")
        and not f.startswith("--optimize=")
        and not f.startswith("-f")
    ]

    compile_only = is_negative_test or expects_runtime_error or is_diagnostic_test

    with tempfile.TemporaryDirectory() as tmp:
        out_path = Path(tmp) / (test_file.stem + "_native_out")
        compile_cmd = [
            str(cccc), "-I./include", *cccc_args, *native_flags,
            "-c=native", "-o", str(out_path), str(test_file),
        ]
        start = time.perf_counter() if bench else None
        try:
            compiled = subprocess.run(
                compile_cmd, capture_output=True, text=True, cwd=script_dir,
                timeout=process_timeout,
            )
        except subprocess.TimeoutExpired:
            return {
                "idx": idx, "test_name": test_name, "exit_code": -1,
                "status": "native_compile_failed", "output": "TIMEOUT",
                "is_negative_test": is_negative_test,
                "expects_runtime_error": expects_runtime_error,
                "stderr_mismatch": None,
                "elapsed": (time.perf_counter() - start) if start else None,
                "vm_profile": None,
            }

        if is_negative_test:
            status = "native_passed" if compiled.returncode != 0 else "native_failed"
            return {
                "idx": idx, "test_name": test_name,
                "exit_code": compiled.returncode, "status": status,
                "output": compiled.stderr, "is_negative_test": True,
                "expects_runtime_error": False, "stderr_mismatch": None,
                "elapsed": None, "vm_profile": None,
            }

        # #1033: a diagnostic-only test that also carries bare --testing in
        # its CCCC_FLAGS (VM backend, the default) never reaches -c=native
        # at all -- cc_run_tests runs the [[cccc::test]] suite in-VM and
        # bails before the native dispatch, by design, so no artifact is
        # ever written even on a fully successful compile+test run. Only
        # require the artifact to exist for the ordinary (non-testing-mode)
        # diagnostic path, where -c=native genuinely does produce one.
        artifact_ok = compiled.returncode == 0 and (
            out_path.exists() or (compile_only and is_testing_mode))
        if not artifact_ok:
            return {
                "idx": idx, "test_name": test_name,
                "exit_code": compiled.returncode,
                "status": "native_compile_failed",
                "output": compiled.stderr or compiled.stdout,
                "is_negative_test": False, "expects_runtime_error": False,
                "stderr_mismatch": None, "elapsed": None, "vm_profile": None,
            }

        if compile_only:
            elapsed = (time.perf_counter() - start) if bench else None
            return {
                "idx": idx, "test_name": test_name, "exit_code": 0,
                "status": "native_passed", "output": "",
                "is_negative_test": False, "expects_runtime_error": False,
                "stderr_mismatch": None, "elapsed": elapsed, "vm_profile": None,
            }

        run_cmd = [str(out_path), *per_test_run_args]
        try:
            run = subprocess.run(
                run_cmd, capture_output=True, text=True, cwd=script_dir,
                timeout=process_timeout,
            )
        except subprocess.TimeoutExpired:
            return {
                "idx": idx, "test_name": test_name, "exit_code": -1,
                "status": "native_failed", "output": "TIMEOUT",
                "is_negative_test": False, "expects_runtime_error": False,
                "stderr_mismatch": None,
                "elapsed": (time.perf_counter() - start) if start else None,
                "vm_profile": None,
            }
        elapsed = (time.perf_counter() - start) if bench else None
        stdout = run.stdout
        output = run.stdout + run.stderr
        if run.returncode == 42:
            status = "native_passed"
        elif expect_stdout is not None and run.returncode == 0:
            status = ("native_passed"
                       if re.search(expect_stdout, stdout, re.MULTILINE | re.DOTALL)
                       else "native_failed")
        else:
            status = "native_failed"
        if status == "native_passed" and reject_stdout is not None:
            if re.search(reject_stdout, stdout, re.MULTILINE | re.DOTALL):
                status = "native_failed"
        return {
            "idx": idx, "test_name": test_name, "exit_code": run.returncode,
            "status": status, "output": output,
            "is_negative_test": False, "expects_runtime_error": False,
            "stderr_mismatch": None, "elapsed": elapsed, "vm_profile": None,
        }
