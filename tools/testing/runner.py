"""Per-test execution: header parsing, subprocess invocation, result classification."""

import re
import subprocess
import time
from pathlib import Path

from . import (
    LEAKS_SKIP_TESTS,
    leak_pass_wants_vm_heap,
    native_audit_skips_enabled,
    vm_profile_path,
)
from .header import parse_test_header
from .native import run_native_roundtrip
from .proc import run_capture
from . import wedge


def run_single_test(idx, test_file, cccc, script_dir, use_leaks, platform, cccc_args,
                    bench=False, profile_dir=None, process_timeout=None,
                    native_mode=False):
    """Run one test file and return a result dict.

    #1202: the whole body runs under wedge.inflight() so a wedge diagnostic
    dump (tools/testing/wedge.py) can name exactly which test file(s) were
    still running when it fired -- printed test order under -j is not
    reliable evidence of that (parallel workers finish out of submission
    order), and thread stacks alone don't carry a test name at all.
    """
    tests_dir = Path(script_dir) / "tests"
    test_name = str(test_file.relative_to(tests_dir))
    with wedge.inflight(test_name):
        return _run_single_test_body(
            idx, test_file, cccc, script_dir, use_leaks, platform, cccc_args,
            bench=bench, profile_dir=profile_dir,
            process_timeout=process_timeout, native_mode=native_mode,
        )


def _run_single_test_body(idx, test_file, cccc, script_dir, use_leaks, platform, cccc_args,
                          bench=False, profile_dir=None, process_timeout=None,
                          native_mode=False):
    tests_dir = Path(script_dir) / "tests"
    test_name = str(test_file.relative_to(tests_dir))

    stdout = ""
    stderr = ""

    header = parse_test_header(test_file)
    is_negative_test = header.is_negative_test
    expects_runtime_error = header.expects_runtime_error
    is_testing_mode = header.is_testing_mode
    is_build_mode = header.is_build_mode
    per_test_flags = header.flags
    per_test_run_args = header.run_args
    expect_stderr = header.expect_stderr
    reject_stderr = header.reject_stderr
    expect_stdout = header.expect_stdout
    reject_stdout = header.reject_stdout
    expect_leak_reason = header.expect_leak
    leak_suppressed = False
    native_skip = header.native_skip
    leaks_keep_vm_heap = header.leaks_keep_vm_heap

    # --build is incompatible with -O<n> (build mode runs the build script
    # in-process and does not compile VM bytecode; the compiler rejects
    # --build combined with -O<n>). Strip it from cccc_args defensively.
    if is_build_mode:
        cccc_args = [a for a in cccc_args
                     if not (a.startswith("-O") and len(a) > 2 and a[2].isdigit())]

    # #1182: --native-audit-skips bypasses CCCC_NATIVE_SKIP the same way it
    # bypasses NATIVE_SKIP_TESTS/NATIVE_SKIP_TESTS_MACOS (native_skip_reason,
    # tools/testing/__init__.py) -- this header directive is the third skip
    # surface that can go stale the same way, and the audit corpus (built
    # from those two tables) doesn't include CCCC_NATIVE_SKIP files anyway,
    # so this only matters if someone runs a full --native-audit-skips
    # sweep over the whole tests/ tree rather than the restricted corpus.
    if native_mode and native_skip and not native_audit_skips_enabled():
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
            platform=platform,
            is_testing_mode=is_testing_mode,
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
                normal_result = run_capture(
                    normal_cmd, cwd=script_dir, timeout=process_timeout,
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
                    leak_result = run_capture(leak_cmd, cwd=script_dir, timeout=30)
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
            result = run_capture(cmd, cwd=script_dir, timeout=process_timeout)
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
        # -j (JSON diagnostics, #966): cc_print_all_errors's "N error(s)
        # generated" summary is suppressed in JSON mode, so a JSON-mode
        # negative test needs its own marker -- the diagnostic object's own
        # severity field.
        or '"severity":"error"' in output
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
