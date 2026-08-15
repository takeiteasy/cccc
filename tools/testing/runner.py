"""Per-test execution: header parsing, subprocess invocation, result classification."""

import re
import subprocess
import time
from pathlib import Path

from . import LEAKS_SKIP_TESTS, leak_pass_wants_vm_heap, vm_profile_path
from .c4 import run_c4_roundtrip
from .native import run_native_roundtrip


def has_matrix_skip(test_file):
    """Return True if test_file carries a CCCC_MATRIX_SKIP annotation in its header."""
    try:
        with open(test_file, "r") as f:
            header_lines = [f.readline() for _ in range(5)]
    except Exception:
        return False
    return any("CCCC_MATRIX_SKIP" in line for line in header_lines)


def run_single_test(idx, test_file, cccc, script_dir, use_leaks, platform, cccc_args,
                    bench=False, c4_mode=False, profile_dir=None, process_timeout=None,
                    matrix_mode=False, native_mode=False):
    """Run one test file and return a result dict.

    matrix_mode: when True, strip any -On flags from per-test CCCC_FLAGS so
    they do not override the matrix sweep flags injected via cccc_args. Tests
    annotated with CCCC_MATRIX_SKIP are skipped entirely in this mode (e.g.
    tests whose correctness depends on a specific -O level the per-pass
    matrix cannot reproduce, since it always forces -O0 plus at most one -f
    pass).
    """
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
    expect_leak_reason = None
    leak_suppressed = False
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
            native_skip = None
            matrix_skip_reason = None
            leaks_keep_vm_heap = False
            for line in header_lines:
                if "CCCC_C4_SKIP" in line:
                    c4_skip = True
                if "CCCC_NATIVE_SKIP" in line:
                    if ":" in line:
                        native_skip = line.split("CCCC_NATIVE_SKIP:", 1)[1].strip().rstrip("*/").strip()
                    else:
                        native_skip = "native-incompatible"
                if "CCCC_LEAKS_KEEP_VM_HEAP" in line:
                    leaks_keep_vm_heap = True
                if "CCCC_EXPECT_LEAK" in line:
                    if ":" in line:
                        expect_leak_reason = line.split("CCCC_EXPECT_LEAK:", 1)[1].strip().rstrip("*/").strip()
                    else:
                        expect_leak_reason = "expected leak"
                if "CCCC_MATRIX_SKIP" in line:
                    if ":" in line:
                        matrix_skip_reason = line.split("CCCC_MATRIX_SKIP:", 1)[1].strip().rstrip("*/").strip()
                    else:
                        matrix_skip_reason = "matrix-incompatible"
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

    # In matrix mode, strip any -On from per-test flags so they cannot
    # override the matrix sweep flags injected via cccc_args.
    if matrix_mode:
        per_test_flags = [
            f for f in per_test_flags
            if not (f.startswith("-O") and len(f) > 2 and f[2].isdigit())
        ]

    if matrix_mode and matrix_skip_reason:
        return {
            "idx": idx,
            "test_name": test_name,
            "exit_code": 0,
            "status": "matrix_skipped",
            "output": "",
            "is_negative_test": is_negative_test,
            "expects_runtime_error": expects_runtime_error,
            "stderr_mismatch": None,
            "elapsed": 0,
            "skip_reason": f"matrix-incompatible: {matrix_skip_reason}",
        }

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

    # --build is incompatible with -O and -f<pass> flags (build mode runs the
    # build script in-process and does not compile VM bytecode at an
    # optimization level or through the peephole/pass pipeline; the compiler
    # rejects --build combined with either). Strip both from cccc_args so
    # matrix sweeps don't break build tests.
    if is_build_mode:
        cccc_args = [a for a in cccc_args
                     if not (a.startswith("-O") and len(a) > 2 and a[2].isdigit())
                     and not a.startswith("-f")]

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

    if native_mode and native_skip:
        return {
            "idx": idx,
            "test_name": test_name,
            "exit_code": 0,
            "status": "native_skipped",
            "output": "",
            "is_negative_test": is_negative_test,
            "expects_runtime_error": expects_runtime_error,
            "stderr_mismatch": None,
            "elapsed": 0,
            "skip_reason": f"native-incompatible: {native_skip}",
        }

    if native_mode:
        # CCCC_EXPECT_STDERR/CCCC_REJECT_STDERR tests already have their
        # diagnostic assertion covered by the normal VM run; under -c=native
        # they only assert that the compile step didn't regress into a
        # build failure (native.py's compile-only tier).
        is_diagnostic_test = expect_stderr is not None or reject_stderr is not None
        return run_native_roundtrip(
            idx, test_file, test_name, cccc, script_dir, cccc_args, per_test_flags,
            per_test_run_args, is_negative_test, expects_runtime_error, bench,
            is_diagnostic_test=is_diagnostic_test,
            expect_stdout=expect_stdout,
            reject_stdout=reject_stdout,
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
            leaks_error = False
            if not skip_leaks:
                # -V makes guest malloc/free visible to `leaks`, but it also
                # disables the VM heap; for tests whose safety flags depend
                # on the VM heap that silently changes what the program does
                # (see LEAKS_VM_HEAP_DEPENDENT_FLAGS) -- cccc now refuses to
                # even start in that combination (#845), so withhold -V
                # there instead. Guest allocations become invisible to
                # `leaks` in that case, but cccc's own host-side allocations
                # are still checked.
                vm_heap_flag = (
                    [] if leak_pass_wants_vm_heap(cccc_args, per_test_flags, leaks_keep_vm_heap)
                    else ["-V"]
                )
                leak_cmd = [
                    "leaks",
                    "-atExit",
                    "--",
                    str(cccc),
                    *vm_heap_flag,
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
            # Parse the real `leaks` summary line rather than a bare "0
            # leaks" substring match, which also matched (and thus hid) a
            # crash report, "leaks timed out", or empty output as if it were
            # a clean run. Anything that doesn't match the summary format is
            # a tool failure, not a verdict either way.
            if skip_leaks:
                is_leaking = False
            else:
                leak_match = re.search(
                    r"(\d+) leaks? for \d+ total leaked bytes", leak_output)
                if leak_match:
                    is_leaking = int(leak_match.group(1)) > 0
                else:
                    is_leaking = False
                    leaks_error = True
            if is_leaking and expect_leak_reason:
                output += f"\n[CCCC_EXPECT_LEAK: {expect_leak_reason}]\n"
                is_leaking = False
                leak_suppressed = True
            cmd = None
        elif platform == "linux":
            profile_json = vm_profile_path(profile_dir, test_name, "source")
            profile_args = ["--vm-profile", "--json"] if profile_json else []
            # See the macOS branch above: withhold -V for tests whose safety
            # flags require the VM heap (#845).
            vm_heap_flag = (
                [] if leak_pass_wants_vm_heap(cccc_args, per_test_flags, leaks_keep_vm_heap)
                else ["-V"]
            )
            cmd = [
                "valgrind",
                "--leak-check=full",
                "--error-exitcode=1",
                "--quiet",
                str(cccc),
                *vm_heap_flag,
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
            vm_heap_flag = (
                [] if leak_pass_wants_vm_heap(cccc_args, per_test_flags, leaks_keep_vm_heap)
                else ["-V"]
            )
            cmd = [
                "drmemory",
                "-batch",
                "-quiet",
                "--",
                str(cccc),
                *vm_heap_flag,
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
        leaks_error = False
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

        leak_suppressed = False
        if is_leaking and expect_leak_reason:
            output += f"\n[CCCC_EXPECT_LEAK: {expect_leak_reason}]\n"
            is_leaking = False
            leak_suppressed = True

    # "TAP version" only appears once execution of a --testing binary has
    # actually started, which is only possible after a successful compile.
    # Past that point, any "expected ... got ..." text is the [[cccc::test]]
    # framework's own assertion-failure wording (src/testing.c, e.g.
    # "expected return value %s %d, got %d"), not a compiler diagnostic —
    # counting it as a compile error misclassified flaky/failing TAP
    # subtests as COMPILATION ERROR and hid the real diagnostic (only the
    # first 3 lines of combined output were ever printed, i.e. just the TAP
    # preamble). Gate the generic "expected"+"got" match on TAP not having
    # started so genuine pre-execution diagnostics phrased that way (e.g.
    # "expected unique generated function name, got existing definition
    # 'foo'") are still caught.
    started_running = "TAP version" in output
    has_compile_error = (
        "error generated" in output
        or "errors generated" in output
        or "cannot open file" in output
        or "undefined function" in output
        or "undefined global" in output
        or "unknown warning option" in output
        or (not started_running and "expected" in output and "got" in output)
    )

    crashed = exit_code in (134, 139, 136, 141, -6, -11, -8, -13)
    stderr_mismatch = None

    if crashed:
        status = "crashed"
    elif is_leaking:
        status = "leak"
    elif leaks_error:
        # `leaks`/valgrind/drmemory produced output that didn't match any
        # recognized summary format (crash report, "leaks timed out", empty
        # output, ...). Distinct from "leak" so it isn't silently reported
        # as a false MEMORY LEAK, and distinct from "passed" so it isn't
        # silently reported as clean either -- it needs a human look (#845).
        status = "leaks_error"
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
    elif is_testing_mode and expect_stderr and exit_code != 0:
        # A --testing run that is deliberately expected to fail/error (e.g.
        # #1007's "zero tests collected" hard error, or #1013's "a runtime
        # safety violation fails the test, not 'ok'"). The expect_stderr
        # regex below validates the actual diagnostic/TAP failure text.
        status = "negative_pass"
    elif is_testing_mode:
        # Compiled and ran, but the [[cccc::test]] framework reported a
        # failing/erroring subtest (nonzero exit, no compile-error markers
        # matched above). Distinct status so the runner can surface the
        # actual TAP failure lines instead of a generic exit-code message.
        status = "test_failed"
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
        "expect_leak_reason": expect_leak_reason if leak_suppressed else None,
    }
