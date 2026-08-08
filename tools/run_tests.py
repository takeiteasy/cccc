#!/usr/bin/env python3
"""Unified CCCC test orchestrator.

Runs all sub-suites in sequence and reports a unified pass/fail summary.
Exit code is non-zero if any sub-suite fails.

Sub-suites:
  source              — main test suite (tools/testing/ package)
  c4                  — .c4 bytecode round-trip (compile → save → reload → run)
  debugger            — macOS host-signal crash-debugger integration (macOS only)
  repl                — interactive REPL PTY integration (POSIX only, ticket #661)
  debugger_condition  — conditional breakpoint PTY integration (POSIX only, ticket 113)
  sqlite              — SQLite 3.53.2 amalgamation smoke test (skips if zip absent)
  header_resolution_smoke — CCCC header resolution from a foreign CWD (ticket #891)
  comptime_native_smoke — native (-m/-c=generated/-c=native) serializer regressions (tickets #892/#897/#901/#904/#918)
  audit_ffi           — src/stdlib FFI registration audit (ticket #784)
  reflection_ffi_check — reflection.h FFI table generation freshness (ticket #859)
  audit_reflection_enums — reflection.h enum values vs internal enums (ticket #860)
  fuzz                — fuzz regression corpus replay, compile-only (ticket #625)

Optional:
  --bench  — run the cross-compiler benchmark after the test suites
  --perf   — run an instrumented VM-opcode pass over tests/benchmarks/ (ticket #625)

Usage:
  python3 tools/run_tests.py [-j N] [--binary PATH] [--quiet] [--process-timeout N]
  python3 tools/run_tests.py --bench               # also run bench.py after tests
  python3 tools/run_tests.py --perf                # also run the perf sub-suite
  ./cccc --build build.c --build-target=test       # build + run everything (calls this script)
"""

import argparse
import json
import os
import subprocess
import sys
import types
from pathlib import Path

# Ensure tools/ is on sys.path so the 'testing' package is importable.
_TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_TOOLS_DIR))

from testing import REPO_ROOT
from testing.platform import detect_platform
from testing.discovery import discover_tests
from testing.suite import run_test_suite_with_isolation
from testing.report import print_summary


def _make_suite_args(quiet=True, bench=False, c4=False, vm_profile=False,
                     process_timeout=None):
    """Return a SimpleNamespace compatible with _run_test_suite's args parameter."""
    return types.SimpleNamespace(
        quiet=quiet,
        bench=bench,
        c4=c4,
        vm_profile=vm_profile,
        process_timeout=process_timeout,
    )


def _run_source_suite(cccc, n_jobs, quiet, process_timeout):
    """Run the main source-mode test suite. Returns (r_dict, ok)."""
    platform = detect_platform()
    tests_dir = REPO_ROOT / "tests"
    test_files = discover_tests(tests_dir)
    args = _make_suite_args(quiet=quiet, process_timeout=process_timeout)
    if not quiet:
        print(f"Running source suite ({len(test_files)} tests)…")
    r = run_test_suite_with_isolation(cccc, REPO_ROOT, False, platform, [], n_jobs, args, test_files)
    ok = r["failed"] == 0 and r["crashed"] == 0
    return r, ok


def _run_c4_suite(cccc, n_jobs, quiet, process_timeout):
    """Run the .c4 bytecode round-trip suite. Returns (r_dict, ok)."""
    platform = detect_platform()
    tests_dir = REPO_ROOT / "tests"
    test_files = discover_tests(tests_dir)
    args = _make_suite_args(quiet=quiet, c4=True, process_timeout=process_timeout)
    if not quiet:
        print(f"Running c4 round-trip suite ({len(test_files)} tests)…")
    r = run_test_suite_with_isolation(cccc, REPO_ROOT, False, platform, [], n_jobs, args, test_files)
    ok = r["failed"] == 0 and r["crashed"] == 0
    return r, ok


def _run_host_signal_suite(cccc):
    """Run the macOS host-signal debugger integration tests.

    Returns (status_str, ok) where status_str is 'passed'/'failed'/'skipped'.
    """
    if sys.platform != "darwin":
        return "skipped (macOS-only)", True

    script = _TOOLS_DIR / "test_host_signal_debugger.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        # Import and call directly to avoid spawning a new interpreter.
        # The script's main() returns 0 or 1 and never calls sys.exit inside.
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_host_signal_debugger", script)
        mod = importlib.util.module_from_spec(spec)

        # Override the global CCCC so the imported module uses our binary.
        mod.__dict__["_OVERRIDE_CCCC"] = str(cccc)
        spec.loader.exec_module(mod)

        # Inject --binary argument so the argparse in main() picks it up.
        old_argv = sys.argv
        sys.argv = ["test_host_signal_debugger.py", "--binary", str(cccc)]
        try:
            rc = mod.main()
        finally:
            sys.argv = old_argv

        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_repl_suite(cccc):
    """Run the interactive REPL PTY integration tests (ticket #661).

    Returns (status_str, ok) where status_str is 'passed'/'failed'/'skipped'.
    The REPL is a stdin-driven interactive session (readline, multi-line
    continuation prompts), so it needs a real pseudo-terminal to exercise --
    the `pty` module is POSIX-only, hence the Windows skip (matching the
    host-signal suite's platform-gating pattern above).
    """
    if sys.platform == "win32":
        return "skipped (POSIX-only, needs a pty)", True

    script = _TOOLS_DIR / "test_repl.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_repl", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        old_argv = sys.argv
        sys.argv = ["test_repl.py", "--binary", str(cccc)]
        try:
            rc = mod.main()
        finally:
            sys.argv = old_argv

        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_debugger_condition_suite(cccc):
    """Run the conditional-breakpoint PTY integration tests (ticket 113).

    Returns (status_str, ok) where status_str is 'passed'/'failed'/'skipped'.
    Same pty-required rationale as the REPL suite above.
    """
    if sys.platform == "win32":
        return "skipped (POSIX-only, needs a pty)", True

    script = _TOOLS_DIR / "test_debugger_condition.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("test_debugger_condition", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        old_argv = sys.argv
        sys.argv = ["test_debugger_condition.py", "--binary", str(cccc)]
        try:
            rc = mod.main()
        finally:
            sys.argv = old_argv

        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_sqlite_suite(cccc):
    """Run the SQLite amalgamation smoke test.

    Returns (status_str, ok). Skips cleanly if the zip is absent.
    """
    script = _TOOLS_DIR / "sqlite_smoke.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("sqlite_smoke", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        # Redirect the CCCC binary used by sqlite_smoke by temporarily
        # patching the module-level 'cccc' variable used in main().
        # sqlite_smoke.main() computes root/cccc from __file__; since
        # we imported via spec there is no __file__ root difference,
        # but we let main() run normally (it will use REPO_ROOT / "cccc").
        # If a custom --binary was passed we need to swap it in.
        rc = mod.main()
        if rc == 0:
            return "passed (or skipped: zip absent)", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_header_resolution_suite():
    """Run the header resolution smoke tests (#891).

    Invokes the built cccc from a temp directory with no include flags, so
    it exercises resolution paths tools/tests.py's own -I./include can't
    reach. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "header_resolution_smoke.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("header_resolution_smoke", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_comptime_native_suite():
    """Run the comptime/native serializer smoke tests (#892).

    The serializer that reconstructs a runtime translation unit only runs
    under -m/-c=generated/-c=native, none of which the VM-only source suite exercises,
    so a serializer regression like #892 (two distinct opaque struct types
    collapsing into "the same type") needs its own smoke test. Returns
    (status_str, ok).
    """
    script = _TOOLS_DIR / "comptime_native_smoke.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("comptime_native_smoke", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_audit_ffi_suite():
    """Run the src/stdlib FFI registration audit (#784).

    Pure source scan -- no cccc binary needed. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "audit_ffi.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("audit_ffi", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_audit_reflection_enums_suite():
    """Run reflection.h TypeKind/NodeKind/AttrTargetKind vs internal enum
    value audit (#860).

    Pure source scan -- no cccc binary needed. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "audit_reflection_enums.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("audit_reflection_enums", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main()
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_reflection_ffi_check():
    """Check src/reflection_ffi_{protos,register}.inc are up to date with
    include/cccc/reflection.h (#859).

    Pure source scan -- no cccc binary needed. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "gen_reflection_ffi.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("gen_reflection_ffi", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main(check=True)
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_fuzz_suite(cccc):
    """Run the fuzz regression corpus replay (#625).

    Compile-only against tests/fuzz/corpus/. Returns (status_str, ok).
    """
    script = _TOOLS_DIR / "fuzz_replay.py"
    if not script.exists():
        return "skipped (script not found)", True

    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("fuzz_replay", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        rc = mod.main(["--binary", str(cccc)])
        if rc == 0:
            return "passed", True
        return "FAILED", False
    except Exception as e:
        return f"FAILED ({e})", False


def _run_perf_suite(cccc):
    """Run an instrumented VM-opcode pass over tests/benchmarks/*.c (#625).

    Informational only -- always returns ok=True; perf numbers on a laptop
    or shared CI runner are too noisy to threshold on. Writes per-benchmark
    JSON to profile/perf/ and prints an aggregate summary: total instructions
    retired per benchmark and the top-N hottest opcodes across the set.
    """
    bench_dir = REPO_ROOT / "tests" / "benchmarks"
    sources = sorted(bench_dir.glob("*.c"))
    if not sources:
        return "skipped (no benchmark sources)", True

    out_dir = REPO_ROOT / "profile" / "perf"
    out_dir.mkdir(parents=True, exist_ok=True)

    totals = {}
    opcode_totals = {}
    for src in sources:
        try:
            r = subprocess.run(
                [str(cccc), "--vm-profile", "--json", "-I./include", str(src)],
                cwd=REPO_ROOT, capture_output=True, text=True, timeout=120,
            )
        except subprocess.TimeoutExpired:
            print(f"  {src.name}: timeout")
            continue

        # Guest stdout (e.g. the benchmark's own "result: N" line) precedes
        # the profiler's JSON blob on stdout -- find where the JSON starts.
        brace = r.stdout.find("{")
        if brace == -1:
            print(f"  {src.name}: no profile JSON produced")
            continue

        try:
            data = json.loads(r.stdout[brace:])
        except json.JSONDecodeError:
            print(f"  {src.name}: malformed profile JSON")
            continue

        out_path = out_dir / f"{src.stem}.json"
        with open(out_path, "w") as f:
            json.dump(data, f, indent=2)

        total_opcodes = data.get("total_opcodes", 0)
        totals[src.stem] = total_opcodes
        for op in data.get("opcodes", []):
            opcode_totals[op["opcode"]] = opcode_totals.get(op["opcode"], 0) + op["count"]

        print(f"  {src.name}: {total_opcodes:,} opcodes retired -> {out_path.relative_to(REPO_ROOT)}")

    if totals:
        print()
        print("  Total instructions retired per benchmark:")
        for name, count in sorted(totals.items(), key=lambda kv: -kv[1]):
            print(f"    {name:<15} {count:>15,}")

        print()
        print("  Top 10 hottest opcodes across the set:")
        for opcode, count in sorted(opcode_totals.items(), key=lambda kv: -kv[1])[:10]:
            print(f"    {opcode:<15} {count:>15,}")

    return f"reported ({len(totals)}/{len(sources)} benchmarks)", True


def _run_bench(cccc):
    """Run the cross-compiler benchmark as a subprocess (bench.py uses sys.exit)."""
    script = _TOOLS_DIR / "bench.py"
    if not script.exists():
        return "skipped (bench.py not found)", True

    result = subprocess.run(
        [sys.executable, str(script), "--cccc", str(cccc)],
        cwd=REPO_ROOT,
    )
    if result.returncode == 0:
        return "passed", True
    return "FAILED", False


def main():
    parser = argparse.ArgumentParser(
        description="Unified CCCC test orchestrator (runs all sub-suites)"
    )
    parser.add_argument(
        "-j", "--jobs", type=int, default=8, help="Parallel jobs for test suite"
    )
    parser.add_argument(
        "--binary", help="Path to cccc binary (used for all sub-suites)"
    )
    parser.add_argument(
        "--quiet", action="store_true",
        help="Suppress per-test output; only show sub-suite summaries"
    )
    parser.add_argument(
        "--process-timeout", type=int, default=None, metavar="SECONDS",
        help="Per-subprocess wall-clock timeout (passed to source and c4 suites)"
    )
    parser.add_argument(
        "--bench", action="store_true",
        help="Run the cross-compiler benchmark after the test suites"
    )
    parser.add_argument(
        "--perf", action="store_true",
        help="Run an instrumented VM-opcode pass over tests/benchmarks/ (informational, never fails)"
    )
    args = parser.parse_args()

    if args.binary:
        cccc = Path(args.binary).resolve()
    else:
        cccc = REPO_ROOT / "cccc"

    if not cccc.exists():
        print(f"Error: {cccc.name} not found. Run 'make' first.", file=sys.stderr)
        sys.exit(1)

    n_jobs = args.jobs if args.jobs else (os.cpu_count() or 1)
    quiet = args.quiet
    timeout = args.process_timeout

    print(f"=== CCCC Test Orchestrator ===")
    print(f"Binary: {cccc}")
    print(f"Jobs:   {n_jobs}")
    print()

    suite_results = {}

    # --- Source suite ---
    print("[ source suite ]")
    r_src, ok_src = _run_source_suite(cccc, n_jobs, quiet, timeout)
    print_summary(r_src, _make_suite_args(quiet=quiet))
    suite_results["source"] = ok_src

    # --- C4 round-trip suite ---
    print()
    print("[ c4 round-trip suite ]")
    r_c4, ok_c4 = _run_c4_suite(cccc, n_jobs, quiet, timeout)
    print_summary(r_c4, _make_suite_args(quiet=quiet, c4=True))
    suite_results["c4"] = ok_c4

    # --- macOS host-signal debugger ---
    print()
    print("[ host-signal debugger ]")
    hsd_status, ok_hsd = _run_host_signal_suite(cccc)
    print(f"  {hsd_status}")
    suite_results["debugger"] = ok_hsd

    # --- REPL PTY integration ---
    print()
    print("[ repl integration ]")
    repl_status, ok_repl = _run_repl_suite(cccc)
    print(f"  {repl_status}")
    suite_results["repl"] = ok_repl

    # --- Conditional breakpoint PTY integration ---
    print()
    print("[ debugger condition integration ]")
    cond_status, ok_cond = _run_debugger_condition_suite(cccc)
    print(f"  {cond_status}")
    suite_results["debugger_condition"] = ok_cond

    # --- SQLite smoke ---
    print()
    print("[ sqlite smoke ]")
    sqlite_status, ok_sqlite = _run_sqlite_suite(cccc)
    print(f"  {sqlite_status}")
    suite_results["sqlite"] = ok_sqlite

    # --- Header resolution smoke (#891) ---
    print()
    print("[ header_resolution_smoke ]")
    hdr_status, ok_hdr = _run_header_resolution_suite()
    print(f"  {hdr_status}")
    suite_results["header_resolution_smoke"] = ok_hdr

    # --- Comptime/native serializer smoke (#892) ---
    print()
    print("[ comptime_native_smoke ]")
    ctn_status, ok_ctn = _run_comptime_native_suite()
    print(f"  {ctn_status}")
    suite_results["comptime_native_smoke"] = ok_ctn

    # --- FFI registration audit ---
    print()
    print("[ audit_ffi ]")
    audit_status, ok_audit = _run_audit_ffi_suite()
    print(f"  {audit_status}")
    suite_results["audit_ffi"] = ok_audit

    # --- Reflection FFI generation check (#859) ---
    print()
    print("[ reflection_ffi_check ]")
    refl_status, ok_refl = _run_reflection_ffi_check()
    print(f"  {refl_status}")
    suite_results["reflection_ffi_check"] = ok_refl

    # --- Reflection enum parity audit (#860) ---
    print()
    print("[ audit_reflection_enums ]")
    enum_status, ok_enum = _run_audit_reflection_enums_suite()
    print(f"  {enum_status}")
    suite_results["audit_reflection_enums"] = ok_enum

    # --- Fuzz regression corpus replay ---
    print()
    print("[ fuzz replay ]")
    fuzz_status, ok_fuzz = _run_fuzz_suite(cccc)
    print(f"  {fuzz_status}")
    suite_results["fuzz"] = ok_fuzz

    # --- Optional bench ---
    if args.bench:
        print()
        print("[ benchmark ]")
        bench_status, ok_bench = _run_bench(cccc)
        print(f"  {bench_status}")
        suite_results["bench"] = ok_bench

    # --- Optional perf (VM opcode profile over tests/benchmarks/) ---
    if args.perf:
        print()
        print("[ perf ]")
        perf_status, ok_perf = _run_perf_suite(cccc)
        print(f"  {perf_status}")
        suite_results["perf"] = ok_perf

    # --- Unified summary ---
    print()
    print("=" * 50)
    print("  Unified Test Summary")
    print("=" * 50)
    all_ok = True
    for name, ok in suite_results.items():
        status = "✓ PASS" if ok else "✗ FAIL"
        print(f"  {name:<12}  {status}")
        if not ok:
            all_ok = False
    print()
    if all_ok:
        print("All sub-suites passed.")
    else:
        failed = [n for n, ok in suite_results.items() if not ok]
        print(f"FAILED sub-suites: {', '.join(failed)}")

    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
