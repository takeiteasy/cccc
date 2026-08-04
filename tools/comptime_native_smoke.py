#!/usr/bin/env python3
"""Comptime/native serializer smoke tests (#892 regression).

`tools/tests.py` runs everything through the VM path, which never touches
src/serialize.c -- the serializer that reconstructs a runtime translation
unit only runs under `-m`/`-G`/`-c=native`. #892 is a serializer bug (two
distinct opaque/incomplete struct types collapsing into "the same type",
see same_type_or_origin() in serialize.c) that a VM-only test structurally
cannot catch, so it needs its own smoke test in the same spirit as
tools/header_resolution_smoke.py.

Cases:
  1. `-c=native` end-to-end: a `@shared` header declaring two distinct
     opaque handles (`typedef struct Alpha Alpha;` / `typedef struct Beta
     Beta;`, the near-universal C opaque-handle idiom) alongside a
     `[[cccc::comptime]]` function in the same file must compile and the
     resulting binary must exit 42 -- not collide on a shared synthesized
     tag (e.g. reflection.h's `AttrTarget`).
  2. `-m` (macro-expanded source) output for that same program must
     mention `struct Alpha` and `struct Beta` by name and must never
     contain `AttrTarget` -- the direct, minimal assertion on the defect.
  3. Two distinct opaque typedefs declared directly in the primary file
     (no header/`@shared` involved) must likewise stay distinct in `-m`
     output -- same_type_or_origin() is a general serializer predicate,
     not `@shared`-specific.
  4. A successful `-c=native` compile+run of case 1's program must emit
     nothing on stderr (regression guard: libbacktrace's warm-up pass used
     to print "no debug info" noise on every run, see host_backtrace.c).
  5. #896 regression: a plain `#include "lib.c"` of a file that itself
     contains `#include @comptime <...>` must build and run under
     `-c=native` -- the auto-captured re-emission of that #include (#891)
     must not hand cccc-only routing syntax to the downstream system
     compiler.

Exit codes: 0 = all cases pass, 1 = any failure.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

SHARED_HEADER = (
    "typedef struct Alpha Alpha;\n"
    "typedef struct Beta Beta;\n"
    "Alpha *mk_a(void);\n"
    "Beta *mk_b(void);\n"
    "int use_a(Alpha *a);\n"
    "int use_b(Beta *b);\n"
)

OPAQUE_PROGRAM = (
    '#include @shared "opaque_handles.h"\n'
    "struct Alpha { int tag; };\n"
    "struct Beta { int tag; };\n"
    "Alpha *mk_a(void){ static struct Alpha a = {1}; return &a; }\n"
    "Beta *mk_b(void){ static struct Beta b = {2}; return &b; }\n"
    "int use_a(Alpha *a){ return a->tag == 1 ? 1 : 20; }\n"
    "int use_b(Beta *b){ return b->tag == 2 ? 2 : 22; }\n"
    "[[cccc::comptime]]\n"
    "Node *check(void) { return MakeIntLiteral(0); }\n"
    "int main(void) {\n"
    "    Alpha *a = mk_a(); Beta *b = mk_b();\n"
    "    return use_a(a) + use_b(b) + check();\n"
    "}\n"
)

PRIMARY_FILE_PROGRAM = (
    "typedef struct Gamma Gamma;\n"
    "typedef struct Delta Delta;\n"
    "Gamma *mk_g(void);\n"
    "Delta *mk_d(void);\n"
    "[[cccc::comptime]]\n"
    "Node *check(void) { return MakeIntLiteral(0); }\n"
    "int main(void) { Gamma *g = mk_g(); Delta *d = mk_d(); (void)g; (void)d; return check(); }\n"
)


def run(cmd, cwd):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)


def write(path: Path, contents: str):
    path.write_text(contents)


def case_native_end_to_end(cccc: Path, tmp: str) -> bool:
    print("  1: -c=native, @shared opaque-handle idiom, two distinct types")
    write(Path(tmp) / "opaque_handles.h", SHARED_HEADER)
    src = Path(tmp) / "opaque_case.c"
    out = Path(tmp) / "opaque_case_out"
    write(src, OPAQUE_PROGRAM)
    result = run([str(cccc), "-c=native", "-o", out.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_dump_expanded_no_attrtarget(cccc: Path, tmp: str) -> bool:
    print("  2: -m output keeps Alpha/Beta distinct, never emits AttrTarget")
    write(Path(tmp) / "opaque_handles.h", SHARED_HEADER)
    src = Path(tmp) / "opaque_dump_case.c"
    write(src, OPAQUE_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    if "AttrTarget" in out:
        print(f"    FAIL: -m output contains AttrTarget\n    {out}")
        return False
    if "struct Alpha" not in out or "struct Beta" not in out:
        print(f"    FAIL: -m output missing struct Alpha/struct Beta\n    {out}")
        return False
    print("    ok")
    return True


def case_primary_file_typedefs_stay_distinct(cccc: Path, tmp: str) -> bool:
    print("  3: two opaque typedefs in the primary file (no @shared) stay distinct")
    src = Path(tmp) / "primary_opaque_case.c"
    write(src, PRIMARY_FILE_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    if "AttrTarget" in out:
        print(f"    FAIL: -m output contains AttrTarget\n    {out}")
        return False
    if "struct Gamma" not in out or "struct Delta" not in out:
        print(f"    FAIL: -m output missing struct Gamma/struct Delta\n    {out}")
        return False
    print("    ok")
    return True


def case_no_stderr_noise(cccc: Path, tmp: str) -> bool:
    print("  4: successful -c=native compile+run emits nothing on stderr")
    write(Path(tmp) / "opaque_handles.h", SHARED_HEADER)
    src = Path(tmp) / "opaque_stderr_case.c"
    out = Path(tmp) / "opaque_stderr_case_out"
    write(src, OPAQUE_PROGRAM)
    result = run([str(cccc), "-c=native", "-o", out.name, src.name], cwd=tmp)
    if result.returncode != 0 or result.stderr.strip():
        print(f"    FAIL: compile exited {result.returncode}, stderr={result.stderr!r}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42 or run_result.stderr.strip():
        print(f"    FAIL: run exited {run_result.returncode}, stderr={run_result.stderr!r}")
        return False
    print("    ok")
    return True


LIB_INCLUDES_COMPTIME = (
    "#include @comptime <dlfcn.h>\n"
    "\n"
    "[[cccc::comptime]]\n"
    "void gen(void) {\n"
    "}\n"
    "gen();\n"
    "\n"
    "int helper(void) { return 42; }\n"
)

MAIN_INCLUDES_LIB = (
    '#include "lib.c"\n'
    "int main(void) { return helper(); }\n"
)


def case_native_include_of_comptime_routed_file(cccc: Path, tmp: str) -> bool:
    print("  5: -c=native, plain #include of a file using @comptime routing (#896)")
    write(Path(tmp) / "lib.c", LIB_INCLUDES_COMPTIME)
    src = Path(tmp) / "main_896.c"
    out = Path(tmp) / "main_896_out"
    write(src, MAIN_INCLUDES_LIB)
    result = run([str(cccc), "-c=native", "-o", out.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def main() -> int:
    root = Path(__file__).parent.parent.resolve()
    cccc = root / "cccc"

    print("Comptime/native serializer smoke tests (#892)")

    if not cccc.exists():
        print(f"  FAIL: {cccc.name} not found — run 'make' first.")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        cases = [
            case_native_end_to_end,
            case_dump_expanded_no_attrtarget,
            case_primary_file_typedefs_stay_distinct,
            case_no_stderr_noise,
            case_native_include_of_comptime_routed_file,
        ]
        results = [case(cccc, tmp) for case in cases]

    if all(results):
        print(f"All {len(results)} comptime/native serializer smoke cases passed.")
        return 0
    print(f"{results.count(False)} of {len(results)} comptime/native serializer smoke cases FAILED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
