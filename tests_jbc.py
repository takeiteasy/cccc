#!/usr/bin/env python3
"""Smoke test for the .jbc bytecode round-trip.

Compiles each test_*.c file in tests/ to a .jbc, then runs the .jbc and
verifies exit code 42. This exercises the cc_save_bytecode / cc_load_bytecode
FFI-table persistence and the cc_load_libc resolution path.

Exit 0 on success, non-zero on failure.
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


JBC_SKIP_TESTS = {
    # These tests depend on dlopen(0, ...) seeing the same libc handle that
    # source-mode FFI setup uses. Bytecode mode rehydrates libc separately.
    "test_ffi_allow_zero.c",
    "test_ffi_deny_zero.c",
    "test_ffi_deny_dlfcn_zero.c",
    "test_ffi_disable_dlfcn_zero.c",
}


def main():
    p = argparse.ArgumentParser(description="JBC roundtrip smoke test")
    p.add_argument("--jcc", default="./jcc")
    p.add_argument("--test", action="append", default=None,
                   help="Specific test file(s) to check (default: all tests/test_*.c)")
    p.add_argument("--include", default="-I./include")
    args = p.parse_args()

    script_dir = Path(__file__).parent.resolve()
    jcc = (script_dir / args.jcc).resolve() if not os.path.isabs(args.jcc) else Path(args.jcc)
    if not jcc.exists():
        print(f"error: jcc binary not found: {jcc}", file=sys.stderr)
        return 1

    if args.test:
        sources = [Path(t).resolve() for t in args.test]
    else:
        sources = sorted((script_dir / "tests").glob("test_*.c"))

    if not sources:
        print("error: no test sources found", file=sys.stderr)
        return 1

    failures = []
    skipped = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        for src in sources:
            if src.name in JBC_SKIP_TESTS:
                skipped += 1
                print(f"  jbc-skip: {src.name}")
                continue
            with open(src) as f:
                header = "".join(f.readline() for _ in range(5))
            if "EXPECT_COMPILE_ERROR" in header or "EXPECT_RUNTIME_ERROR" in header:
                skipped += 1
                continue
            per_test_flags = []
            for line in header.splitlines():
                if "JCC_FLAGS:" in line:
                    flags_str = line.split("JCC_FLAGS:", 1)[1].strip().rstrip("*/").strip()
                    per_test_flags = flags_str.split()
                    break
            jbc = tmpdir / (src.stem + ".jbc")
            save_cmd = [str(jcc), args.include, *per_test_flags, "-o", str(jbc), str(src)]
            save = subprocess.run(save_cmd, capture_output=True, text=True, cwd=script_dir)
            if save.returncode != 0:
                failures.append((src, f"save failed (rc={save.returncode}): {save.stderr.strip()[:200]}"))
                continue
            run_cmd = [str(jcc), str(jbc)]
            run = subprocess.run(run_cmd, capture_output=True, text=True, cwd=script_dir)
            if run.returncode != 42:
                failures.append((src, f"run failed (rc={run.returncode}): {run.stderr.strip()[:200]}"))
                continue
            print(f"  jbc-ok: {src.name}")
    if failures:
        print(f"\n{len(failures)} failures:", file=sys.stderr)
        for src, msg in failures:
            print(f"  {src.name}: {msg}", file=sys.stderr)
        return 1
    print(f"\nAll {len(sources) - skipped} jbc roundtrips passed ({skipped} negative tests skipped).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
