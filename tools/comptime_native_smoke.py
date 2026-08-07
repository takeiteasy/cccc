#!/usr/bin/env python3
"""Native-backend serializer smoke tests (#892/#897/#901/#904/#918 regressions).

`tools/tests.py` runs everything through the VM path, which never touches
src/serialize.c -- the serializer that reconstructs a runtime translation
unit only runs under `-m`/`-G`/`-c=native`. Despite the module name (kept
for history: it started as a comptime-specific regression test), this
script now covers native-backend serializer bugs in general -- anything a
VM-only test structurally cannot catch -- in the same spirit as
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
  6. #897 regression: a struct passed *by value* to a function must
     serialize as `struct Point`, not `struct <param-name>` -- a
     by-value parameter used to be enough to trigger struct_decl()
     re-installing the tag under the parameter's own declarator name
     (see the comment on struct_union_decl in src/parse.c).
  7. #897 regression, union variant of case 6.
  8. #897 regression: a file-scope struct referenced from two functions
     (by pointer) plus a by-value parameter in a third, all in one
     program -- the same root cause also mis-owned the struct's
     definition itself (emitted inside the first referencing function
     instead of at file scope), not only by-value parameters.
  9. #897 regression, direct assertion: `-m` output for case 6's program
     must name the real tag (`struct Point`) and must never contain the
     bogus tag (`struct q`, named after the parameter's own identifier) --
     the same style of direct assertion as case 2 for #892.
  10. #901 regression: a bodiless function declaration written in the
      primary file (e.g. `int abs(int x);`, no #include, no definition
      elsewhere) must compile and run under `-c=native` -- the VM path
      resolves such a call as an FFI symbol with no native declaration
      needed, but the downstream system compiler does, and the prototype
      used to be silently dropped from the generated C entirely.
  11. #901 regression, direct assertion: `-m` output for case 10's program
      must contain the real parameter type (`int abs(int`), not a
      parameter-less signature -- serialize_function_signature used to
      degrade a bodiless declaration's params to `(void)` since only a
      parsed body populates the Obj-based parameter list it read.
  12. #901 regression guard: a program that `#include <stdio.h>` and calls
      `printf` must NOT get a serialized `printf` prototype in `-m` output
      -- the re-emitted `#include` already supplies it; only primary-file
      (or cccc-only-routed) declarations should be serialized.
  13. #901 regression: `extern int g;` in the primary file must serialize
      as `extern int g;` in `-m` output, never a bare `int g;` (which is a
      tentative *definition* that collides with the real symbol at link
      time).
  14. #904 regression: a program that `#include <stdio.h>`/`<errno.h>` and
      references `stdout`/`errno` must compile and run under `-c=native`
      -- those macros expand (during preprocessing, before this backend
      ever sees the AST) into calls to internal accessor shims
      (`__cccc_stdout()`, `__cccc_errno_ptr()`) with no native counterpart;
      the fix (`serialize_native_accessor_shims`) defines each one actually
      used in terms of the real re-emitted symbol.
  15. #904 regression, direct assertion: `-m` output for case 14's program
      must contain `return stdout;` and `return &errno;` shim bodies.
  16. #918 regression: pointer arithmetic (subscripting, `p + n`, `q - p`,
      multi-dimensional array indexing) and every global initializer shape
      it exercises (scalar, struct, string, address-of-another-global via a
      Relocation) must compile and run under `-c=native`, with the exit
      code depending on the arithmetic being correct (`q - p == 3`), not
      just on the program compiling.
  17. #918 regression, direct assertion: `-m` output for case 16's program
      must contain no `/* init data */` placeholder and no pointer-typed
      cast on a scaled byte offset (the two `-c=native`-rejects-plain-C
      defects #918 reported), and must use `(char *)`-based pointer
      arithmetic.
  18. #918 regression: a union global initialized through a non-largest
      member (`union { char c; long l; } u = {.c = 3};`) must compile and
      run under `-c=native`, reconstructed via its largest member.
  19. #918 regression, direct assertion: `-m` output for case 18's program
      names the largest member (`.l =`), not the one actually written
      (`.c`).
  20. #918 regression: a union whose largest-by-size member doesn't span
      the union's full (alignment-padded) size has no member that can
      losslessly reconstruct it -- `-c=native` must fail with a named
      `cannot serialize` diagnostic, never a placeholder or a guessed
      initializer that silently changes the program's data.

Exit codes: 0 = all cases pass, 1 = any failure.
"""

import re
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


STRUCT_BYVAL_PROGRAM = (
    "struct Point { int x; int y; };\n"
    "int helper(struct Point q) { return q.x + q.y; }\n"
    "int main(void) {\n"
    "    struct Point z = {40, 2};\n"
    "    return helper(z);\n"
    "}\n"
)

UNION_BYVAL_PROGRAM = (
    "union U { int x; float f; };\n"
    "int helper(union U q) { return q.x; }\n"
    "int main(void) {\n"
    "    union U z; z.x = 42;\n"
    "    return helper(z);\n"
    "}\n"
)

STRUCT_MULTI_USE_PROGRAM = (
    "struct Point { int x; int y; };\n"
    "int f(struct Point *p) { return p->x; }\n"
    "int g(struct Point *p) { return p->y; }\n"
    "int helper(struct Point q) { return q.x + q.y; }\n"
    "int main(void) {\n"
    "    struct Point z = {40, 2};\n"
    "    return (f(&z) + g(&z) + helper(z)) - 42;\n"
    "}\n"
)


def _native_run_case(cccc: Path, tmp: str, name: str, program: str) -> bool:
    src = Path(tmp) / f"{name}.c"
    out = Path(tmp) / f"{name}_out"
    write(src, program)
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


def case_struct_byval_param(cccc: Path, tmp: str) -> bool:
    print("  6: -c=native, struct-by-value parameter serializes with the real tag (#897)")
    return _native_run_case(cccc, tmp, "struct_byval_897", STRUCT_BYVAL_PROGRAM)


def case_union_byval_param(cccc: Path, tmp: str) -> bool:
    print("  7: -c=native, union-by-value parameter serializes with the real tag (#897)")
    return _native_run_case(cccc, tmp, "union_byval_897", UNION_BYVAL_PROGRAM)


def case_struct_multi_use_and_byval(cccc: Path, tmp: str) -> bool:
    print("  8: -c=native, struct used by-pointer in two functions and by-value in a third (#897)")
    return _native_run_case(cccc, tmp, "struct_multi_897", STRUCT_MULTI_USE_PROGRAM)


def case_struct_byval_m_output(cccc: Path, tmp: str) -> bool:
    print("  9: -m output for a struct-by-value parameter names the real tag, never 'struct q' (#897)")
    src = Path(tmp) / "struct_byval_dump_897.c"
    write(src, STRUCT_BYVAL_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    if "struct q" in out:
        print(f"    FAIL: -m output contains the bogus tag 'struct q'\n    {out}")
        return False
    if "struct Point" not in out:
        print(f"    FAIL: -m output missing struct Point\n    {out}")
        return False
    print("    ok")
    return True


BODILESS_DECL_PROGRAM = (
    "int abs(int x);\n"
    "int main(void) { return abs(-1) + 41; }\n"
)

PRINTF_INCLUDE_PROGRAM = (
    "#include <stdio.h>\n"
    "int main(void) { printf(\"%d\\n\", 1); return 42; }\n"
)

EXTERN_GLOBAL_PROGRAM = (
    "extern int g;\n"
    "int g = 5;\n"
    "int main(void) { return g == 5 ? 42 : 1; }\n"
)


def case_bodiless_decl_end_to_end(cccc: Path, tmp: str) -> bool:
    print("  10: -c=native, primary-file bodiless declaration compiles and runs (#901)")
    return _native_run_case(cccc, tmp, "bodiless_decl_901", BODILESS_DECL_PROGRAM)


def case_bodiless_decl_m_output(cccc: Path, tmp: str) -> bool:
    print("  11: -m output for a bodiless declaration keeps its real parameter type (#901)")
    src = Path(tmp) / "bodiless_decl_dump_901.c"
    write(src, BODILESS_DECL_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    if "int abs(int" not in out:
        print(f"    FAIL: -m output missing 'int abs(int ...)' prototype\n    {out}")
        return False
    print("    ok")
    return True


def case_header_decl_not_reemitted(cccc: Path, tmp: str) -> bool:
    print("  12: -m output never serializes a header-sourced declaration like printf (#901 regression guard)")
    src = Path(tmp) / "printf_include_dump_901.c"
    write(src, PRINTF_INCLUDE_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    if "int printf(" in out:
        print(f"    FAIL: -m output serialized a printf prototype\n    {out}")
        return False
    print("    ok")
    return True


def case_extern_global_m_output(cccc: Path, tmp: str) -> bool:
    print("  13: -m output for an extern global keeps 'extern', never a bare tentative definition (#901)")
    src = Path(tmp) / "extern_global_dump_901.c"
    write(src, EXTERN_GLOBAL_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    if "extern int g;" not in out:
        print(f"    FAIL: -m output missing 'extern int g;'\n    {out}")
        return False
    print("    ok")
    return True


ACCESSOR_SHIM_PROGRAM = (
    "#include <stdio.h>\n"
    "#include <errno.h>\n"
    "int main(void) { (void)stdout; return errno == 0 ? 42 : 1; }\n"
)


def case_accessor_shim_end_to_end(cccc: Path, tmp: str) -> bool:
    print("  14: -c=native, stdout/errno macro-expanded accessor shims compile and run (#904)")
    return _native_run_case(cccc, tmp, "accessor_shim_904", ACCESSOR_SHIM_PROGRAM)


def case_accessor_shim_m_output(cccc: Path, tmp: str) -> bool:
    print("  15: -m output defines the accessor shims actually used, in terms of the real symbol (#904)")
    src = Path(tmp) / "accessor_shim_dump_904.c"
    write(src, ACCESSOR_SHIM_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    if "return stdout;" not in out:
        print(f"    FAIL: -m output missing '__cccc_stdout' shim body\n    {out}")
        return False
    if "return &errno;" not in out:
        print(f"    FAIL: -m output missing '__cccc_errno_ptr' shim body\n    {out}")
        return False
    print("    ok")
    return True


PTR_ARITH_INIT_PROGRAM = (
    "int arr[5] = {1,2,3,4,5};\n"
    "struct S { int a; short b; };\n"
    "struct S gs = {7, 9};\n"
    "int *gptr = &arr[2];\n"
    "const char *gp = \"lit\";\n"
    "int mat[2][3] = {{1,2,3},{4,5,6}};\n"
    "int main(void) {\n"
    "    int *a = arr, *q = arr + 3;\n"
    "    if (a[0] != 1 || q[-1] != 3) return 1;\n"
    "    if (q - a != 3) return 2;\n"
    "    if (gs.a != 7 || gs.b != 9) return 3;\n"
    "    if (*gptr != 3) return 4;\n"
    "    if (gp[0] != 'l') return 5;\n"
    "    if (mat[1][2] != 6) return 6;\n"
    "    return 42;\n"
    "}\n"
)

UNION_LARGEST_MEMBER_PROGRAM = (
    "union U { char c; long l; };\n"
    "union U u = {.c = 3};\n"
    "int main(void) { return (u.l & 0xff) == 3 ? 42 : 1; }\n"
)


def case_ptr_arith_and_init_end_to_end(cccc: Path, tmp: str) -> bool:
    print("  16: -c=native, pointer arithmetic + global initializer reconstruction (#918)")
    return _native_run_case(cccc, tmp, "ptr_arith_init_918", PTR_ARITH_INIT_PROGRAM)


def case_ptr_arith_and_init_m_output(cccc: Path, tmp: str) -> bool:
    print("  17: -m output for #918's program has no bogus pointer-cast offset or placeholder initializer")
    src = Path(tmp) / "ptr_arith_init_dump_918.c"
    write(src, PTR_ARITH_INIT_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    if "/* init data */" in out:
        print(f"    FAIL: -m output still contains the '/* init data */' placeholder\n    {out}")
        return False
    # The reported bug's exact shape: a `+`/`-` operator directly followed
    # by a pointer-typed cast wrapping a parenthesized offset expression,
    # e.g. `+ (int *)(0 * 4)` -- `ptr + int` re-cast to a pointer type,
    # which real gcc/clang reject as "invalid operands". A cast further out
    # (e.g. `(int *)((char *)a + 0 * 4)`, this fix's own output) doesn't
    # match: there the `+` is followed by a bare integer expression, not a
    # cast.
    if re.search(r"[+-]\s*\([A-Za-z_][\w ]*\*\)\(", out):
        print(f"    FAIL: -m output still casts a scaled offset to a pointer type\n    {out}")
        return False
    if "(char *)" not in out:
        print(f"    FAIL: -m output missing the expected (char *) pointer-arithmetic base\n    {out}")
        return False
    print("    ok")
    return True


def case_union_largest_member(cccc: Path, tmp: str) -> bool:
    print("  18: -c=native, union global initializer reconstructs via its largest member (#918)")
    return _native_run_case(cccc, tmp, "union_largest_918", UNION_LARGEST_MEMBER_PROGRAM)


def case_union_largest_member_m_output(cccc: Path, tmp: str) -> bool:
    print("  19: -m output for a union global names the largest member ('.l', not '.c') (#918)")
    src = Path(tmp) / "union_largest_dump_918.c"
    write(src, UNION_LARGEST_MEMBER_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    if ".l =" not in out:
        print(f"    FAIL: -m output missing the largest-member designator '.l ='\n    {out}")
        return False
    print("    ok")
    return True


def case_unserializable_union_hard_errors(cccc: Path, tmp: str) -> bool:
    print("  20: -c=native, a union whose largest member doesn't span alignment padding fails loudly (#918)")
    src = Path(tmp) / "union_no_full_member_918.c"
    # union { char c[5]; int x; }: by raw size char[5] (5 bytes) is the
    # largest member, but int's 4-byte alignment pads the union itself to
    # 8 bytes -- char[5] does not span the full object, so no member here
    # can losslessly reconstruct it. Must fail loudly (a named diagnostic),
    # not emit a plausible-but-wrong initializer or a placeholder the host
    # compiler chokes on.
    write(src, (
        "union U { char c[5]; int x; };\n"
        "union U u = {.c = {1,2,3,4,5}};\n"
        "int main(void) { return 42; }\n"
    ))
    result = run([str(cccc), "-c=native", "-o", "union_no_full_member_918_out", src.name], cwd=tmp)
    if result.returncode == 0:
        print("    FAIL: compile succeeded; expected a hard 'cannot serialize' diagnostic")
        return False
    if "cannot serialize" not in result.stderr:
        print(f"    FAIL: expected a 'cannot serialize' diagnostic on stderr, got:\n    {result.stderr}")
        return False
    print("    ok")
    return True


def main() -> int:
    root = Path(__file__).parent.parent.resolve()
    cccc = root / "cccc"

    print("Native-backend serializer smoke tests (#892/#897/#901/#904/#918)")

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
            case_struct_byval_param,
            case_union_byval_param,
            case_struct_multi_use_and_byval,
            case_struct_byval_m_output,
            case_bodiless_decl_end_to_end,
            case_bodiless_decl_m_output,
            case_header_decl_not_reemitted,
            case_extern_global_m_output,
            case_accessor_shim_end_to_end,
            case_accessor_shim_m_output,
            case_ptr_arith_and_init_end_to_end,
            case_ptr_arith_and_init_m_output,
            case_union_largest_member,
            case_union_largest_member_m_output,
            case_unserializable_union_hard_errors,
        ]
        results = [case(cccc, tmp) for case in cases]

    if all(results):
        print(f"All {len(results)} native-backend serializer smoke cases passed.")
        return 0
    print(f"{results.count(False)} of {len(results)} native-backend serializer smoke cases FAILED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
