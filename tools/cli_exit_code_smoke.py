#!/usr/bin/env python3
"""CLI exit-code smoke tests (#1260 regression).

`tools/tests.py` classifies negative tests by scanning stderr for a
diagnostic string (tools/testing/runner.py), not by the driver's process
exit status, so a `goto BAIL` that prints an error but leaves `exit_code`
at its default 0 sails straight through the per-test-file suite. #1260
reported that class for the serialize-and-exit modes. This script drives
the built `cccc` through the non-compile exit paths in main.c and asserts
each one exits non-zero once it has printed an error.

Cases:
  1. `-c=generated` to an unwritable output path (the -m/-c=generated
     bail-out).
  2. `-E` to an unwritable output path (preprocess-only bail-out).
  3. `--ffi-decls` to an unwritable output path (JSON bail-out).
  4. `-c=native` to an unwritable output path (run_native_backend).
  5. `-c=generated` of a program that fails to compile: the serialize-and-
     exit path must still surface the non-zero status (regression floor --
     this one already worked via longjmp, guard it stays that way).
  6. Sanity floor: a well-formed `-c=generated` compile still exits 0 and
     still prints "Generated C written to".
  7. #1266: an all-comptime TU that publishes nothing and defines no
     ordinary function still serializes to a valid near-empty .gen.c under
     `-c=generated` (exit 0, file written) instead of "failed to link
     programs" with a bogus exit 0 and no output.
  8. #1266: the same TU without `-c=generated` -- nothing to compile or
     run -- exits non-zero with the "defines no functions or variables"
     driver diagnostic (the per-test-file harness can't assert a bare
     driver error + non-zero exit outside --build/--testing mode).

Exit codes: 0 = all cases pass, 1 = any failure.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

UNWRITABLE = "/nonexistent-cccc-smoke-dir/out.x"
GOOD_SRC = "int main(void) { return 42; }\n"
BAD_SRC = "int main(void) { return nope_undeclared + 1; }\n"
# An all-comptime TU: the comptime code runs and prints, but publishes no
# node and there is no ordinary function, so parse() yields an empty global
# list and cc_link_progs returns NULL (#1266).
EMPTY_TU_SRC = (
    "#include @comptime <stdio.h>\n"
    "[[cccc::comptime]] int helper(int x){ return x * 2; }\n"
    "[[cccc::comptime]] void gen(void){ printf(\"%d\\n\", helper(21)); }\n"
    "gen();\n"
)


def run(cmd, cwd=None):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd,
                              stdin=subprocess.DEVNULL, timeout=120)
    except subprocess.TimeoutExpired as e:
        return subprocess.CompletedProcess(cmd, 124, "", f"timed out: {e}")


def _expect_nonzero(label, result):
    if result.returncode == 0:
        print(f"    FAIL: {label} exited 0\n    stderr={result.stderr!r}")
        return False
    print("    ok")
    return True


def case_generated_unwritable(cccc, tmp):
    print("  1: -c=generated to an unwritable -o path")
    src = Path(tmp) / "g1.c"
    src.write_text(GOOD_SRC)
    return _expect_nonzero(
        "generated/unwritable",
        run([str(cccc), "-c=generated", src.name, "-o", UNWRITABLE], cwd=tmp))


def case_preprocess_unwritable(cccc, tmp):
    print("  2: -E to an unwritable -o path")
    src = Path(tmp) / "e1.c"
    src.write_text(GOOD_SRC)
    return _expect_nonzero(
        "-E/unwritable",
        run([str(cccc), "-E", src.name, "-o", UNWRITABLE], cwd=tmp))


def case_ffi_decls_unwritable(cccc, tmp):
    print("  3: --ffi-decls to an unwritable -o path")
    src = Path(tmp) / "j1.c"
    src.write_text(GOOD_SRC)
    return _expect_nonzero(
        "--ffi-decls/unwritable",
        run([str(cccc), "--ffi-decls", src.name, "-o", UNWRITABLE], cwd=tmp))


def case_native_unwritable(cccc, tmp):
    print("  4: -c=native to an unwritable -o path")
    src = Path(tmp) / "n1.c"
    src.write_text(GOOD_SRC)
    return _expect_nonzero(
        "native/unwritable",
        run([str(cccc), "-c=native", src.name, "-o", UNWRITABLE], cwd=tmp))


def case_generated_compile_error(cccc, tmp):
    print("  5: -c=generated of a program that fails to compile")
    src = Path(tmp) / "bad.c"
    src.write_text(BAD_SRC)
    out = Path(tmp) / "bad.gen.c"
    return _expect_nonzero(
        "generated/compile-error",
        run([str(cccc), "-c=generated", src.name, "-o", out.name], cwd=tmp))


def case_good_still_zero(cccc, tmp):
    print("  6: a well-formed -c=generated compile still exits 0")
    src = Path(tmp) / "ok.c"
    src.write_text(GOOD_SRC)
    out = Path(tmp) / "ok.gen.c"
    result = run([str(cccc), "-c=generated", src.name, "-o", out.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: exit {result.returncode}\n    stderr={result.stderr!r}")
        return False
    if "Generated C written to" not in result.stderr:
        print(f"    FAIL: missing confirmation line\n    stderr={result.stderr!r}")
        return False
    print("    ok")
    return True


def case_empty_tu_generated_ok(cccc, tmp):
    print("  7: all-comptime TU (publishes nothing) still serializes clean")
    src = Path(tmp) / "empty7.c"
    src.write_text(EMPTY_TU_SRC)
    out = Path(tmp) / "empty7.gen.c"
    result = run([str(cccc), "-c=generated", src.name, "-o", out.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: exit {result.returncode}\n    stderr={result.stderr!r}")
        return False
    if "failed to link programs" in result.stderr:
        print(f"    FAIL: still reports link failure\n    stderr={result.stderr!r}")
        return False
    if not (Path(tmp) / "empty7.gen.c").exists():
        print("    FAIL: no output file written")
        return False
    print("    ok")
    return True


def case_empty_tu_plain_error(cccc, tmp):
    print("  8: all-comptime TU without -c=generated exits non-zero")
    src = Path(tmp) / "empty8.c"
    src.write_text(EMPTY_TU_SRC)
    result = run([str(cccc), src.name], cwd=tmp)
    if result.returncode == 0:
        print(f"    FAIL: exited 0\n    stderr={result.stderr!r}")
        return False
    if "defines no functions or variables" not in result.stderr:
        print(f"    FAIL: missing driver diagnostic\n    stderr={result.stderr!r}")
        return False
    print("    ok")
    return True


def main():
    root = Path(__file__).parent.parent.resolve()
    cccc = root / "cccc"

    print("CLI exit-code smoke tests (#1260)")
    if not cccc.exists():
        print(f"  FAIL: {cccc.name} not found -- run 'make' first.")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        cases = [
            case_generated_unwritable,
            case_preprocess_unwritable,
            case_ffi_decls_unwritable,
            case_native_unwritable,
            case_generated_compile_error,
            case_good_still_zero,
            case_empty_tu_generated_ok,
            case_empty_tu_plain_error,
        ]
        results = [c(cccc, tmp) for c in cases]

    if all(results):
        print(f"All {len(results)} CLI exit-code smoke cases passed.")
        return 0
    print(f"{results.count(False)} of {len(results)} CLI exit-code smoke cases FAILED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
