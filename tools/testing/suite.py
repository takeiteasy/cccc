"""Parallel test suite executor."""

import concurrent.futures
import threading
from pathlib import Path

from .runner import run_single_test


def _run_test_suite(cccc, script_dir, use_leaks, platform, cccc_args, n_jobs, args,
                    test_files, header=None):
    """Execute test files in parallel and return aggregate results dict."""
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
            str(profile_dir) if profile_dir else None,
            process_timeout,
            getattr(args, "native", False),
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
    native_passed = 0
    native_failed = 0
    native_skipped = 0
    native_compile_failed = 0
    failed_tests = []
    crashed_tests = []
    native_skipped_tests = []
    timings = []

    quiet = getattr(args, "quiet", False)

    def print_single_result(result):
        nonlocal total, passed, failed, crashed, negative_passed
        nonlocal native_passed, native_failed, native_skipped, native_compile_failed
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
                for line in output.splitlines()[:10]:
                    print(f"  {line}")
        elif status == "test_failed":
            # Compiled and ran under --testing, but a subtest failed/errored
            # (nonzero exit). Surface the TAP "not ok" lines plus their
            # diagnostic follow-up so the actual failure is visible instead
            # of just the exit code.
            failed += 1
            failed_tests.append(f"{test_name} (TEST FAILED, exit {exit_code})")
            if not quiet:
                print(f"✗ {test_name} (TEST FAILED, exit {exit_code}){timing_str}")
                lines = output.splitlines()
                shown = 0
                for i, line in enumerate(lines):
                    if line.startswith("not ok"):
                        for follow in lines[i:i + 6]:
                            print(f"  {follow}")
                            shown += 1
                        if shown >= 18:
                            break
                if shown == 0:
                    # No TAP "not ok" markers found (e.g. crashed before
                    # emitting one) — fall back to the tail of the output.
                    for line in lines[-10:]:
                        print(f"  {line}")
        elif status == "leak":
            failed += 1
            failed_tests.append(f"{test_name} (MEMORY LEAK)")
            if not quiet:
                print(f"💧 {test_name} (MEMORY LEAK){timing_str}")
                leak_lines = [line for line in output.splitlines() if "Leak:" in line][:3]
                for line in leak_lines:
                    print(f"  {line}")
        elif status == "leaks_error":
            # The leak tool itself produced unparseable output (crash
            # report, timeout, empty output, ...) rather than a real
            # leak/no-leak verdict. Reported as a failure so it gets a human
            # look, but kept distinct from "leak" so it is never confused
            # with a genuine MEMORY LEAK finding (#845).
            failed += 1
            failed_tests.append(f"{test_name} (LEAKS TOOL ERROR)")
            if not quiet:
                print(f"⚠️  {test_name} (LEAKS TOOL ERROR){timing_str}")
                for line in output.splitlines()[-5:]:
                    print(f"  {line}")
        elif status == "negative_pass":
            negative_passed += 1
            if not quiet:
                leak_note = ""
                if result.get("expect_leak_reason"):
                    leak_note = f" [expected leak: {result['expect_leak_reason']}]"
                if result["is_negative_test"]:
                    print(f"✓ {test_name} (correctly rejected invalid code){timing_str}{leak_note}")
                else:
                    print(f"✓ {test_name} (correctly detected runtime error){timing_str}{leak_note}")
        elif status == "passed":
            passed += 1
            if not quiet:
                leak_note = ""
                if result.get("expect_leak_reason"):
                    leak_note = f" [expected leak: {result['expect_leak_reason']}]"
                print(f"✓ {test_name}{timing_str}{leak_note}")
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
        elif status == "native_passed":
            native_passed += 1
            if not quiet:
                print(f"✓ {test_name}{timing_str}")
        elif status == "native_skipped":
            native_skipped += 1
            reason = result.get("skip_reason", "")
            if reason:
                native_skipped_tests.append(f"{test_name} ({reason})")
        elif status == "native_compile_failed":
            native_compile_failed += 1
            failed += 1
            failed_tests.append(f"{test_name} (NATIVE COMPILE FAILED)")
            if not quiet:
                print(f"✗ {test_name} (NATIVE COMPILE FAILED){timing_str}")
                for line in output.splitlines()[-10:]:
                    print(f"  {line}")
        elif status == "native_failed":
            native_failed += 1
            failed += 1
            failed_tests.append(f"{test_name} (NATIVE RUNTIME FAILED, exit {exit_code})")
            if not quiet:
                print(f"✗ {test_name} (NATIVE RUNTIME FAILED, exit {exit_code}){timing_str}")
                for line in output.splitlines()[-10:]:
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
        "native_passed": native_passed,
        "native_failed": native_failed,
        "native_skipped": native_skipped,
        "native_compile_failed": native_compile_failed,
        "failed_tests": failed_tests,
        "crashed_tests": crashed_tests,
        "native_skipped_tests": native_skipped_tests,
        "timings": timings,
    }


_SUM_KEYS = (
    "total", "passed", "failed", "crashed", "negative_passed",
    "native_passed", "native_failed", "native_skipped", "native_compile_failed",
)
_LIST_KEYS = (
    "failed_tests", "crashed_tests",
    "native_skipped_tests", "timings",
)


def merge_suite_results(a, b):
    """Combine two _run_test_suite() result dicts into one for reporting."""
    merged = {k: a[k] + b[k] for k in _SUM_KEYS}
    merged.update({k: a[k] + b[k] for k in _LIST_KEYS})
    return merged


# tests/suites/test_suite_posix.c's fork/signal-timing subtests are prone to
# scheduler-starvation flakiness when several other CPU-bound test-file
# processes run concurrently under -j (#853 -- confirmed by direct
# reproduction: running N copies of the compiled test binary concurrently
# fails intermittently, sequential runs never do). Running it in its own
# serial pass, sequenced after the rest of the parallel batch has finished,
# removes that contention for the one file that's sensitive to it.
#
# test_pthread_mutex.c joined this set while building #967's --native mode:
# under -c=native its compiled child is a real OS-thread binary (unlike the
# VM's own thread simulation), so it hits the same #853-shaped scheduler
# contention under a full -j8 --native run (intermittent nonzero exit;
# passes every time standalone or at -j1). Isolating it here fixes the
# flake without adding a native-specific skip -- the test itself is correct.
ISOLATED_SERIAL_TESTS = frozenset({"test_suite_posix.c", "test_pthread_mutex.c"})


def run_test_suite_with_isolation(cccc, script_dir, use_leaks, platform, cccc_args,
                                  n_jobs, args, test_files, header=None):
    """_run_test_suite(), but ISOLATED_SERIAL_TESTS run in their own -j1 pass
    after the rest of the batch. Every caller (tools/testing/cli.py,
    tools/run_tests.py) must go through this, not _run_test_suite directly,
    or the isolation silently doesn't apply."""
    isolated_files = [t for t in test_files if t.name in ISOLATED_SERIAL_TESTS]
    parallel_files = [t for t in test_files if t.name not in ISOLATED_SERIAL_TESTS]

    if not (isolated_files and n_jobs > 1 and parallel_files):
        return _run_test_suite(
            cccc, script_dir, use_leaks, platform, cccc_args,
            n_jobs, args, test_files, header=header,
        )

    r = _run_test_suite(
        cccc, script_dir, use_leaks, platform, cccc_args,
        n_jobs, args, parallel_files, header=header,
    )
    isolated_header = "[ isolated (serial) tests -- see #853 ]"
    if header:
        isolated_header = f"{header} {isolated_header}"
    r_isolated = _run_test_suite(
        cccc, script_dir, use_leaks, platform, cccc_args,
        1, args, isolated_files,
        header=isolated_header if not getattr(args, "quiet", False) else None,
    )
    return merge_suite_results(r, r_isolated)
