#!/usr/bin/env python3
"""Native-backend serializer smoke tests (#892/#897/#901/#904/#918/#925/#926/#927/#928/#952/#953/#956/#963/#964/#968 regressions).

`tools/tests.py` runs everything through the VM path, which never touches
src/serialize.c -- the serializer that reconstructs a runtime translation
unit only runs under `-m`/`-c=generated`/`-c=native`. Despite the module name (kept
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
  21. #925/#926/#927 regression: a `-c=native` compile+run exercising all
      three at once -- a static local array read back across two calls, a
      file-scope pointer initialized from a non-char-array compound literal
      (`(int[]){...}`), a file-scope struct pointer initialized from
      `&(struct S){...}`, two sibling `for (int i = 0; ...)` loops plus a
      nested one reusing `i`, a multi-declarator `for (int i = 0, j = 10;
      ...)` init, and a nested block re-declaring a parameter's own name
      (legal C -- shadows the parameter, must not rename it) -- with the
      exit code depending on every one of those being semantically correct,
      not just on the program compiling.
  22. #925/#926/#927 regression, direct assertion: `-m` output for case 21's
      program contains no raw `.L..` synthesized name, no `unsupported
      expr` placeholder, a `static`-qualified definition for the renamed
      static-local array and the renamed compound-literal struct global, no
      function that declares the same local name twice, and the
      parameter-shadowing function's prototype/definition signature still
      reads `shadow_param(int x)` (the parameter itself must never be
      renamed -- its signature is already printed by the time the
      collision check runs).
  23. #925/#926 regression guard: `-m` output for the tickets' own repro,
      `tests/suites/test_suite_arrays.c`, contains neither a raw `.L..`
      name nor an `unsupported expr` placeholder.
  24. #928 regression: `-c=generated` output for a comptime macro that builds
      file-scope anon globals via `CompoundLiteral`/`InitArray`/
      `InitStruct` (`reflect_new_anon_gvar()`'s other two call sites,
      besides `MakeStringLiteral` which #925 already covered) contains no
      raw `.L..` name, has real forward-declared definitions, and -- the
      only real proof -- compiles cleanly through the host `cc`.

  35: #952 regression: `-m` output for an anonymous struct type used as a
      union member (the near-universal tagged-union idiom) must print the
      real member body, never `va_list` -- find_anonymous_typedef_name()
      used to return the first same-kind tagless typedef in scope (e.g.
      stdarg.h's own tagless `va_list` struct) without checking it actually
      names the type being serialized.
  36: #953 regression: `-c=generated` output for a type reached via
      `GetType()` from a comptime function, where that type is also defined
      in a plain `#include`d header, must not duplicate the definition --
      the auto-captured `#include` already supplies it (a hard
      "redefinition" error otherwise).
  37: #953 regression guard: the same case, but the header is reached only
      via `#include @comptime` (never auto-captured) -- the definition must
      still be re-derived, since nothing else supplies it. Also asserts the
      `@comptime`-routed `#include` itself is never leaked into the output.

Exit codes: 0 = all cases pass, 1 = any failure.
"""

import os
import re
import shutil
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

# The correct-match branch of OPAQUE_PROGRAM's main() is 1 + 2 + 0 == 3; the
# "+ 39" keeps the exit code at the repo's usual 42-on-success (any mismatch
# branch lands well away from 42, e.g. the all-mismatch 20 + 22 + 0 + 39 ==
# 81). Before #925 fixed static-local serialization, mk_a()/mk_b() returned a
# struct pointer aliased onto a bogus string-literal global -- reading ->tag
# through it never matched, and the mismatch sum (20 + 22 + 0) happened to
# equal 42 on its own, masking the very bug this case exists to catch.
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
    "    return use_a(a) + use_b(b) + check() + 39;\n"
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


# shadow_param(): a nested block re-declaring a parameter's own name (legal
# C -- the inner `x` shadows the parameter for its block's extent). #926's
# rename-on-collision must rename the *shadowing local*, never the
# parameter -- serialize_function_signature has already printed the
# parameter's name by the time the collision check runs, so renaming the
# parameter instead would desync the signature from the body.
ANON_LOCALS_PROGRAM = (
    "struct S { int a; int b; };\n"
    "\n"
    "static int *get_static_array(void) {\n"
    "    static int arr[3];\n"
    "    arr[0] = 10; arr[1] = 20; arr[2] = 12;\n"
    "    return arr;\n"
    "}\n"
    "\n"
    "int *gp = (int[]){10, 20, 12};\n"
    "struct S *gs = &(struct S){1, 2};\n"
    "\n"
    "static int shadow_param(int x) {\n"
    "    { int x = 5; return x; }\n"
    "}\n"
    "\n"
    "static int sum_loops(void) {\n"
    "    int total = 0;\n"
    "    for (int i = 0; i < 3; i++) { total += i; }\n"
    "    for (int i = 0; i < 4; i++) { total += i * 10; }\n"
    "    for (int i = 0, j = 10; i < 2; i++, j--) { total += j; }\n"
    "    for (int i = 0; i < 2; i++) {\n"
    "        for (int i = 0; i < 2; i++) { total += 1; }\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "int main(void) {\n"
    "    int *arr1 = get_static_array();\n"
    "    if (arr1[0] != 10 || arr1[1] != 20 || arr1[2] != 12) return 1;\n"
    "    int *arr2 = get_static_array();\n"
    "    if (arr2[0] != 10) return 2;\n"
    "    if (gp[0] != 10 || gp[1] != 20 || gp[2] != 12) return 3;\n"
    "    if (gs->a != 1 || gs->b != 2) return 4;\n"
    "    if (sum_loops() != 86) return 5;\n"
    "    if (shadow_param(1) != 5) return 6;\n"
    "    return 42;\n"
    "}\n"
)


def case_anon_locals_end_to_end(cccc: Path, tmp: str) -> bool:
    print("  21: -c=native, static local + compound-literal globals + sibling/nested for-loop locals (#925/#926/#927)")
    return _native_run_case(cccc, tmp, "anon_locals_925_926_927", ANON_LOCALS_PROGRAM)


def case_anon_locals_m_output(cccc: Path, tmp: str) -> bool:
    print("  22: -m output for #925/#926/#927's program has no dotted names, no unsupported-expr placeholder, no duplicate local declarations")
    src = Path(tmp) / "anon_locals_dump_925_926_927.c"
    write(src, ANON_LOCALS_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    # A crashed/erroring `-m` invocation can leave `out` empty (or truncated),
    # which would vacuously satisfy every "does not contain X" check below --
    # require a successful run with recognisable output first.
    if result.returncode != 0 or "int main(void)" not in out:
        print(f"    FAIL: -m exited {result.returncode} or produced unrecognisable output\n    {result.stderr}\n    {out}")
        return False
    if ".L.." in out:
        print(f"    FAIL: -m output still contains a raw '.L..' synthesized name\n    {out}")
        return False
    if "unsupported expr" in out:
        print(f"    FAIL: -m output still contains an 'unsupported expr' placeholder\n    {out}")
        return False
    if "static int __cccc_arr" not in out and not re.search(r"static int __cccc_\w*arr\w*\[3\]", out):
        print(f"    FAIL: -m output missing a static, internally-linked definition for the renamed static local array\n    {out}")
        return False
    if not re.search(r"static struct S __cccc_\w+ = \{ \.a = 1, \.b = 2 \}", out):
        print(f"    FAIL: -m output missing a static definition for the renamed compound-literal global\n    {out}")
        return False
    # Every declared local name in each function must be unique -- the
    # collision this case exercises (#926) previously emitted the same
    # `int i;` declaration twice in a row.
    fn_bodies = re.findall(r"\bsum_loops\(void\) \{(.*?)\n\}", out, re.DOTALL)
    if not fn_bodies:
        print(f"    FAIL: could not locate sum_loops()'s body in -m output to check for duplicate declarations\n    {out}")
        return False
    for fn_body in fn_bodies:
        decl_names = re.findall(r"^\s+int (\w+);$", fn_body, re.MULTILINE)
        if len(decl_names) != len(set(decl_names)):
            print(f"    FAIL: sum_loops() still declares a colliding local name twice: {decl_names}\n    {out}")
            return False
    # shadow_param()'s parameter must never be renamed -- both the
    # prototype and the definition's signature must still read
    # `shadow_param(int x)`, matching what serialize_function_signature
    # already committed to output before the collision-renaming loop runs.
    if len(re.findall(r"shadow_param\(int x\)", out)) != 2:
        print(f"    FAIL: shadow_param()'s parameter was renamed out from under its own signature\n    {out}")
        return False
    print("    ok")
    return True


GVAR_BUILDERS_G_PROGRAM = (
    "struct FPt { int x; int y; };\n"
    "\n"
    "[[cccc::comptime]]\n"
    "Node *gen_gvar_cl(void) {\n"
    "    Type *pt_ty = GetType(\"FPt\");\n"
    "    Type *int_ty = GetType(\"int\");\n"
    "    Node *gpt = CompoundLiteral(pt_ty, MakeIntLiteral(7), MakeIntLiteral(13));\n"
    "    Obj *fn = MakeFunction(\"gvar_cl_x\", int_ty);\n"
    "    WithFn(fn) {\n"
    "        FunctionSetBody(fn, MakeReturn(MakeMember(gpt, \"x\")));\n"
    "    }\n"
    "    return MakeIntLiteral(0);\n"
    "}\n"
    "gen_gvar_cl();\n"
    "\n"
    "[[cccc::comptime]]\n"
    "Node *gen_gvar_arr(void) {\n"
    "    Type *int_ty = GetType(\"int\");\n"
    "    Node *garr = InitArray(int_ty,\n"
    "        MakeIntLiteral(10), MakeIntLiteral(20), MakeIntLiteral(30));\n"
    "    Obj *fn = MakeFunction(\"gvar_arr_elem2\", int_ty);\n"
    "    WithFn(fn) {\n"
    "        FunctionSetBody(fn, MakeReturn(MakeSubscript(garr, MakeIntLiteral(2))));\n"
    "    }\n"
    "    return MakeIntLiteral(0);\n"
    "}\n"
    "gen_gvar_arr();\n"
    "\n"
    "[[cccc::comptime]]\n"
    "Node *gen_gvar_struct(void) {\n"
    "    Type *pt_ty = GetType(\"FPt\");\n"
    "    Type *int_ty = GetType(\"int\");\n"
    "    const char *flds[] = {\"x\"};\n"
    "    Node *vals[] = {MakeIntLiteral(99)};\n"
    "    Node *gs = InitStruct(pt_ty, flds, vals, 1);\n"
    "    Obj *fn_x = MakeFunction(\"gvar_struct_x\", int_ty);\n"
    "    WithFn(fn_x) {\n"
    "        FunctionSetBody(fn_x, MakeReturn(MakeMember(gs, \"x\")));\n"
    "    }\n"
    "    Obj *fn_y = MakeFunction(\"gvar_struct_y\", int_ty);\n"
    "    WithFn(fn_y) {\n"
    "        FunctionSetBody(fn_y, MakeReturn(MakeMember(gs, \"y\")));\n"
    "    }\n"
    "    return MakeIntLiteral(0);\n"
    "}\n"
    "gen_gvar_struct();\n"
)


def case_gvar_builders_generated_output(cccc: Path, tmp: str) -> bool:
    print("  24: -c=generated output for file-scope CompoundLiteral/InitArray/InitStruct anon globals is valid, compilable C (#928)")
    src = Path(tmp) / "gvar_builders_928.c"
    write(src, GVAR_BUILDERS_G_PROGRAM)
    result = run([str(cccc), "-c=generated", src.name], cwd=tmp)
    gen = Path(tmp) / "a.gen.c"
    # Same vacuous-pass trap as cases 22/23: a crashed/erroring -c=generated
    # invocation must not silently satisfy the "does not contain X" checks
    # below.
    out = gen.read_text() if result.returncode == 0 and gen.exists() else ""
    if result.returncode != 0 or "gvar_cl_x" not in out:
        print(f"    FAIL: -c=generated exited {result.returncode} or produced unrecognisable output\n    {result.stderr}\n    {out}")
        return False
    if ".L.." in out:
        print(f"    FAIL: -c=generated output still contains a raw '.L..' synthesized name\n    {out}")
        return False
    if not re.search(r"static struct FPt __cccc_\w+;", out):
        print(f"    FAIL: -c=generated output missing a forward-declared static struct definition\n    {out}")
        return False
    if not re.search(r"static int __cccc_\w+\[3\];", out):
        print(f"    FAIL: -c=generated output missing a forward-declared static array definition\n    {out}")
        return False
    # The only real proof the output is valid C: hand it to the host
    # compiler directly (#928's array-cast bug -- `(int [3])` instead of
    # `(int *)` -- compiled as -m/-c=generated text just fine; only a real
    # `cc -c` rejects it).
    obj = Path(tmp) / "gvar_builders_928.o"
    cc_result = subprocess.run(["cc", "-x", "c", "-c", "-", "-o", str(obj)],
                                input=out, capture_output=True, text=True, cwd=tmp)
    if cc_result.returncode != 0:
        print(f"    FAIL: host cc rejected the -c=generated output\n    {cc_result.stderr}\n    {out}")
        return False
    print("    ok")
    return True


def case_arrays_suite_no_serializer_gaps(cccc: Path, tmp: str) -> bool:
    print("  23: -m output for tests/suites/test_suite_arrays.c (the #925/#926 repro) has no dotted names or unsupported-expr placeholders")
    suite = Path(__file__).parent.parent / "tests" / "suites" / "test_suite_arrays.c"
    result = run([str(cccc), "-m", str(suite)], cwd=tmp)
    out = result.stdout
    # Same vacuous-pass trap as case 22: a crashed/erroring `-m` invocation
    # must not silently satisfy the two "does not contain" checks below.
    if result.returncode != 0 or "get_static_array" not in out:
        print(f"    FAIL: -m exited {result.returncode} or produced unrecognisable output\n    {result.stderr}")
        return False
    if ".L.." in out:
        print(f"    FAIL: -m output still contains a raw '.L..' synthesized name")
        return False
    if "unsupported expr" in out:
        print(f"    FAIL: -m output still contains an 'unsupported expr' placeholder")
        return False
    print("    ok")
    return True


def case_bare_c_defaults_to_native_a_out(cccc: Path, tmp: str) -> bool:
    print("  25: bare -c (no FMT, no -o) defaults to native and writes ./a.out")
    src = Path(tmp) / "bare_c_native.c"
    write(src, "int main(void) { return 42; }\n")
    a_out = Path(tmp) / "a.out"
    if a_out.exists():
        a_out.unlink()
    result = run([str(cccc), "-c", src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    if not a_out.exists():
        print(f"    FAIL: ./a.out was not written\n    {result.stderr}")
        return False
    run_result = run(["./a.out"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: ./a.out exited {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_bare_c_bytecode_defaults_to_a_c4(cccc: Path, tmp: str) -> bool:
    print("  26: -c=bytecode with no -o writes ./a.c4")
    src = Path(tmp) / "bare_c_bytecode.c"
    write(src, "int main(void) { return 42; }\n")
    a_c4 = Path(tmp) / "a.c4"
    if a_c4.exists():
        a_c4.unlink()
    result = run([str(cccc), "-c=bytecode", src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    if not a_c4.exists():
        print(f"    FAIL: ./a.c4 was not written\n    {result.stderr}")
        return False
    run_result = run([str(cccc), "a.c4"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: running a.c4 exited {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_emit_cccc_native_requires_explicit_cc(cccc: Path, tmp: str) -> bool:
    print("  27: -c=native --emit-cccc with no CCCC_NATIVE_CC is rejected")
    src = Path(tmp) / "emit_cccc_no_cc.c"
    write(src, "void use(int * [[cccc::single]] p) { (void)p; }\n"
               "int main(void) { int x = 5; use(&x); return 42; }\n")
    out = Path(tmp) / "emit_cccc_no_cc_out"
    env = dict(os.environ)
    env.pop("CCCC_NATIVE_CC", None)
    result = subprocess.run(
        [str(cccc), "-c=native", "--emit-cccc", "-o", out.name, src.name],
        capture_output=True, text=True, cwd=tmp, env=env,
    )
    if result.returncode == 0:
        print("    FAIL: compile unexpectedly succeeded with no CCCC_NATIVE_CC")
        return False
    if "CCCC_NATIVE_CC" not in result.stderr:
        print(f"    FAIL: expected a CCCC_NATIVE_CC diagnostic\n    {result.stderr}")
        return False
    print("    ok")
    return True


def case_emit_cccc_native_with_explicit_cc(cccc: Path, tmp: str) -> bool:
    print("  28: -c=native --emit-cccc with CCCC_NATIVE_CC=cc set compiles and runs "
          "(checked-pointer qualifiers degrade to an ignorable unknown attribute)")
    src = Path(tmp) / "emit_cccc_with_cc.c"
    write(src, "void use(int * [[cccc::single]] p) { (void)p; }\n"
               "int main(void) { int x = 5; use(&x); return 42; }\n")
    out = Path(tmp) / "emit_cccc_with_cc_out"
    env = dict(os.environ)
    env["CCCC_NATIVE_CC"] = "cc"
    result = subprocess.run(
        [str(cccc), "-c=native", "--emit-cccc", "-o", out.name, src.name],
        capture_output=True, text=True, cwd=tmp, env=env,
    )
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_emit_cccc_m_output_round_trips(cccc: Path, tmp: str) -> bool:
    print("  29: -m --emit-cccc output (with a checked-pointer qualifier) recompiles under cccc itself and runs")
    src = Path(tmp) / "emit_cccc_roundtrip.c"
    write(src, "void use(int * [[cccc::single]] p) { (void)p; }\n"
               "int main(void) { int x = 5; use(&x); return 42; }\n")
    dump = run([str(cccc), "-m", "--emit-cccc", src.name], cwd=tmp)
    if dump.returncode != 0 or "[[cccc::single]]" not in dump.stdout:
        print(f"    FAIL: -m --emit-cccc exited {dump.returncode} or dropped the qualifier\n    {dump.stderr}")
        return False
    roundtrip = Path(tmp) / "emit_cccc_roundtrip_gen.c"
    write(roundtrip, dump.stdout)
    run_result = run([str(cccc), roundtrip.name], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: recompiled output exited {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_test_run_clean_program_compiles(cccc: Path, tmp: str) -> bool:
    print("  30: --test-run on a clean program compiles and the artifact runs (exit 42)")
    src = Path(tmp) / "test_run_clean.c"
    write(src, "int main(void) { return 42; }\n")
    out = Path(tmp) / "test_run_clean_out"
    if out.exists():
        out.unlink()
    result = run([str(cccc), "--test-run", "-o", out.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    if not out.exists():
        print(f"    FAIL: artifact was not written\n    {result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: artifact exited {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_test_run_oob_write_refused(cccc: Path, tmp: str) -> bool:
    print("  31: --test-run refuses to compile a program that OOB-writes under max safety")
    src = Path(tmp) / "test_run_oob.c"
    write(src, "#include <stdlib.h>\n"
               "int main(void) {\n"
               "    int *p = malloc(sizeof(int) * 4);\n"
               "    p[10] = 5;\n"
               "    free(p);\n"
               "    return 42;\n"
               "}\n")
    out = Path(tmp) / "test_run_oob_out"
    if out.exists():
        out.unlink()
    result = run([str(cccc), "--test-run", "-o", out.name, src.name], cwd=tmp)
    if result.returncode == 0:
        print("    FAIL: compile unexpectedly succeeded for an OOB-writing program")
        return False
    if out.exists():
        print("    FAIL: artifact was written despite the refusal")
        return False
    print("    ok")
    return True


def case_test_run_basic_level_compiles(cccc: Path, tmp: str) -> bool:
    print("  32: --test-run=basic compiles a program only max-level bounds checks would catch "
          "(proves level selection)")
    src = Path(tmp) / "test_run_basic.c"
    write(src, "#include <stdlib.h>\n"
               "int main(void) {\n"
               "    int *p = malloc(sizeof(int) * 4);\n"
               "    p[10] = 5;\n"
               "    free(p);\n"
               "    return 42;\n"
               "}\n")
    out = Path(tmp) / "test_run_basic_out"
    if out.exists():
        out.unlink()
    result = run([str(cccc), "--test-run=basic", "-o", out.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: artifact exited {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_test_run_bytecode_no_global_contamination(cccc: Path, tmp: str) -> bool:
    print("  33: --test-run -c=bytecode's smoke-test execution doesn't contaminate the saved "
          "bytecode's global initializers (fork isolation)")
    src = Path(tmp) / "test_run_contam.c"
    write(src, "int g = 5;\n"
               "int main(void) {\n"
               "    int was = g;\n"
               "    g = 999;\n"
               "    return was;\n"
               "}\n")
    out = Path(tmp) / "test_run_contam.c4"
    if out.exists():
        out.unlink()
    result = run([str(cccc), "--test-run", "-c=bytecode", "-o", out.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    # A freshly-loaded run of the saved .c4 must see g's compile-time
    # initializer (5), not the smoke-test run's post-execution value (999)
    # -- if the smoke test had run in-process instead of in a forked child,
    # cc_save_bytecode() would have serialized the mutated live vm state.
    run_result = run([str(cccc), out.name], cwd=tmp)
    if run_result.returncode != 5:
        print(f"    FAIL: reloaded .c4 exited {run_result.returncode}, expected 5 "
              f"(global contamination from the smoke-test run)\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_c_generated_defaults_and_aliases(cccc: Path, tmp: str) -> bool:
    print("  34: -c=generated with no -o writes ./a.gen.c; -cgen/-cg/--compile=generated "
          "alias to the same target; -G is now rejected (#936)")
    src = Path(tmp) / "c_generated_936.c"
    # -c=generated only serializes macro-generated content (generated_only=true
    # in cc_serialize_program) plus auto-captured directives, not ordinary
    # functions -- a plain `int main(){}` alone would produce near-empty
    # output, so this needs an actual [[cccc::comptime]] generator.
    write(src, "[[cccc::comptime]]\n"
               "void gen(void) {\n"
               "    Obj *fn = MakeFunction(\"c_generated_936_answer\", GetType(\"int\"));\n"
               "    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));\n"
               "    PublishNode(fn);\n"
               "}\n"
               "gen();\n"
               "int main(void) { return c_generated_936_answer(); }\n")

    # Default filename, no -o.
    a_gen_c = Path(tmp) / "a.gen.c"
    if a_gen_c.exists():
        a_gen_c.unlink()
    result = run([str(cccc), "-c=generated", src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: -c=generated exited {result.returncode}\n    {result.stderr}")
        return False
    if not a_gen_c.exists():
        print(f"    FAIL: ./a.gen.c was not written\n    {result.stderr}")
        return False
    baseline = a_gen_c.read_text()
    if "c_generated_936_answer" not in baseline:
        print(f"    FAIL: a.gen.c missing expected content\n    {baseline}")
        return False

    # Aliases must produce byte-identical output to the canonical spelling.
    for alias_args, label in (
        (["-cgen"], "-cgen"),
        (["-cg"], "-cg"),
        (["--compile=generated"], "--compile=generated"),
    ):
        out = Path(tmp) / f"alias_{label.strip('-=')}.c"
        r = run([str(cccc), *alias_args, "-o", out.name, src.name], cwd=tmp)
        if r.returncode != 0:
            print(f"    FAIL: {label} exited {r.returncode}\n    {r.stderr}")
            return False
        if out.read_text() != baseline:
            print(f"    FAIL: {label} output differs from -c=generated's")
            return False

    # -G is no longer a recognized option.
    g_result = run([str(cccc), "-G", src.name], cwd=tmp)
    if g_result.returncode == 0:
        print(f"    FAIL: -G was accepted; it should have been removed by #936")
        return False

    # --emit-only and --attr-target still apply under -c=generated (accepted,
    # no error) -- test_emit_only_suppresses_auto_capture.c and
    # test_attr_target_msvc.c cover their precise effects elsewhere.
    et_out = Path(tmp) / "attr_target.c"
    et_result = run([str(cccc), "-c=generated", "--attr-target=gnu",
                      "--emit-only", "-o", et_out.name, src.name], cwd=tmp)
    if et_result.returncode != 0:
        print(f"    FAIL: -c=generated --attr-target=gnu --emit-only exited "
              f"{et_result.returncode}\n    {et_result.stderr}")
        return False

    print("    ok")
    return True


LOBJ_UNION_PROGRAM = (
    "#include <stdarg.h>\n"
    "struct LObj {\n"
    "    int tag;\n"
    "    union {\n"
    "        struct { const char *name; } atom;\n"
    "        struct { struct LObj *car, *cdr; } pair;\n"
    "    } as;\n"
    "};\n"
    "struct LObj g;\n"
    "int main(void) { return 42; }\n"
)


def case_anon_union_member_not_va_list(cccc: Path, tmp: str) -> bool:
    print("  35: -m output for an anonymous struct used as a union member "
          "prints the real body, never 'va_list' (#952)")
    src = Path(tmp) / "lobj_union_952.c"
    write(src, LOBJ_UNION_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    if result.returncode != 0:
        print(f"    FAIL: -m exited {result.returncode}\n    {result.stderr}")
        return False
    if "va_list" in out:
        print(f"    FAIL: -m output still mis-prints a union member as va_list\n    {out}")
        return False
    if "const char *name" not in out or "struct LObj *car" not in out:
        print(f"    FAIL: -m output missing the real anonymous member bodies\n    {out}")
        return False
    obj = Path(tmp) / "lobj_union_952.o"
    cc_result = subprocess.run(["cc", "-x", "c", "-c", "-", "-o", str(obj)],
                                input=out, capture_output=True, text=True, cwd=tmp)
    if cc_result.returncode != 0:
        print(f"    FAIL: host cc rejected the -m output\n    {cc_result.stderr}\n    {out}")
        return False
    print("    ok")
    return True


LOBJ_TYPES_HEADER = (
    "typedef struct LObj {\n"
    "    int tag;\n"
    "    double value;\n"
    "} LObj;\n"
)

LOBJ_COMPTIME_PROGRAM = (
    "[[cccc::comptime]]\n"
    "void gen(void) {\n"
    "    Type *t = GetType(\"LObj\");\n"
    "    Obj *fn = MakeFunction(\"touch_lobj\", GetType(\"int\"));\n"
    "    FunctionAddParam(fn, \"p\", MakePointer(t));\n"
    "    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(0)));\n"
    "    PublishNode(fn);\n"
    "}\n"
    "gen();\n"
    "int main(void) { return touch_lobj((void *)0) == 0 ? 42 : 1; }\n"
)


def case_generated_no_duplicate_captured_include(cccc: Path, tmp: str) -> bool:
    print("  36: -c=generated output for a GetType()'d struct reached via a "
          "plain #include has no duplicate definition (#953)")
    write(Path(tmp) / "lobj_types_953.h", LOBJ_TYPES_HEADER)
    src = Path(tmp) / "lobj_953a.c"
    write(src, '#include "lobj_types_953.h"\n' + LOBJ_COMPTIME_PROGRAM)
    out_path = Path(tmp) / "lobj_953a.gen.c"
    result = run([str(cccc), "-c=generated", "-o", out_path.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: -c=generated exited {result.returncode}\n    {result.stderr}")
        return False
    out = out_path.read_text()
    if out.count("struct LObj {") != 0 or "typedef struct LObj" in out:
        # The captured #include supplies the definition; a re-derived typedef
        # would still be dropped in generated_only mode, but a re-derived
        # *struct body* (from the tagless typedef falling back to
        # find_anonymous_typedef_name-less inline emission) must not appear.
        print(f"    FAIL: -c=generated output re-derives LObj's definition "
              f"on top of the captured #include\n    {out}")
        return False
    if "touch_lobj" not in out or '#include "lobj_types_953.h"' not in out:
        print(f"    FAIL: -c=generated output missing expected content\n    {out}")
        return False
    obj = Path(tmp) / "lobj_953a.o"
    cc_result = subprocess.run(["cc", "-c", out_path.name, "-o", str(obj)],
                                capture_output=True, text=True, cwd=tmp)
    if cc_result.returncode != 0:
        print(f"    FAIL: host cc rejected the -c=generated output\n    {cc_result.stderr}\n    {out}")
        return False
    print("    ok")
    return True


def case_generated_comptime_include_still_derives(cccc: Path, tmp: str) -> bool:
    print("  37: -c=generated output for a GetType()'d struct reached only "
          "via #include @comptime still emits its definition (#953 guard)")
    write(Path(tmp) / "lobj_types_953b.h", LOBJ_TYPES_HEADER)
    src = Path(tmp) / "lobj_953b.c"
    write(src, '#include @comptime "lobj_types_953b.h"\n' + LOBJ_COMPTIME_PROGRAM)
    out_path = Path(tmp) / "lobj_953b.gen.c"
    result = run([str(cccc), "-c=generated", "-o", out_path.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: -c=generated exited {result.returncode}\n    {result.stderr}")
        return False
    out = out_path.read_text()
    if "struct LObj {" not in out:
        print(f"    FAIL: -c=generated output dropped LObj's definition -- "
              f"its @comptime-routed #include is never captured, so nothing "
              f"else supplies it\n    {out}")
        return False
    if '#include @comptime' in out or '#include "lobj_types_953b.h"' in out:
        print(f"    FAIL: -c=generated output leaked the @comptime-routed "
              f"#include verbatim\n    {out}")
        return False
    obj = Path(tmp) / "lobj_953b.o"
    cc_result = subprocess.run(["cc", "-c", out_path.name, "-o", str(obj)],
                                capture_output=True, text=True, cwd=tmp)
    if cc_result.returncode != 0:
        print(f"    FAIL: host cc rejected the -c=generated output\n    {cc_result.stderr}\n    {out}")
        return False
    print("    ok")
    return True


MUTUAL_RECURSION_PROGRAM = """
[[cccc::comptime]]
void gen(void) {
    Type *t = GetType("int");
    Obj *c = FunctionPrototype("gen_c", t);
    FunctionAddParam(c, "x", t);
    PublishNode(c);

    Obj *a = MakeFunction("gen_a", t);
    FunctionAddParam(a, "x", t);
    Obj *b = MakeFunction("gen_b", t);
    FunctionAddParam(b, "x", t);
    PublishNode(a);
    PublishNode(b);
    WithFn(a) {
        Node *xr = MakeParamRef(a, "x");
        FunctionSetBody(a, Quote("{ return gen_b($1); }", xr));
    }
    WithFn(b) {
        Node *xr = MakeParamRef(b, "x");
        FunctionSetBody(b, Quote("{ return gen_a($1); }", xr));
    }
}
gen();

int main(void) { return 42; }
"""


def case_generated_forward_decls_hoisted(cccc: Path, tmp: str) -> bool:
    print("  38: -c=generated forward-declares a generated function's "
          "callee even when published earlier in program order, and never "
          "drops a body-less published prototype (#956)")
    src = Path(tmp) / "mutual_956.c"
    write(src, MUTUAL_RECURSION_PROGRAM)
    out_path = Path(tmp) / "mutual_956.gen.c"
    result = run([str(cccc), "-c=generated", "-o", out_path.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: -c=generated exited {result.returncode}\n    {result.stderr}")
        return False
    out = out_path.read_text()
    if "gen_c(int x);" not in out:
        print(f"    FAIL: -c=generated output dropped gen_c's never-defined "
              f"published prototype\n    {out}")
        return False
    a_def = out.find("gen_a(int x) {")
    b_decl = out.find("gen_b(int x);")
    if a_def == -1 or b_decl == -1 or b_decl > a_def:
        print(f"    FAIL: gen_a's definition (created before gen_b) calls "
              f"gen_b, but gen_b's forward declaration does not precede it "
              f"-- mutual recursion is not satisfied by either creation "
              f"order\n    {out}")
        return False
    obj = Path(tmp) / "mutual_956.o"
    cc_result = subprocess.run(["cc", "-c", out_path.name, "-o", str(obj)],
                                capture_output=True, text=True, cwd=tmp)
    if cc_result.returncode != 0:
        print(f"    FAIL: host cc rejected the -c=generated output\n    {cc_result.stderr}\n    {out}")
        return False
    print("    ok")
    return True


# The #963b group below asserts VM 42 -> native 42, not just "the -m output
# looks right". That distinction is the whole point of the ticket: an
# unhandled node kind in *statement* position used to serialize as
# `/* unsupported expr kind N */;` -- a valid null statement -- so the native
# binary compiled cleanly and silently returned a different answer than the
# VM. Only running the binary catches that class, and only running *both*
# sides establishes that the two agree rather than that the native side
# happens to hit 42 on its own.


def _vm_and_native_run_case(cccc: Path, tmp: str, name: str, program: str) -> bool:
    src = Path(tmp) / f"{name}.c"
    write(src, program)
    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False
    return _native_run_case(cccc, tmp, name, program)

BITOPS_PROGRAM = (
    "int main(void) {\n"
    "    int a = __builtin_popcount(7);\n"
    "    int b = __builtin_clz(1u);\n"
    "    int c = __builtin_ctz(8u);\n"
    "    int d = __builtin_ffs(4);\n"
    "    unsigned e = __builtin_bswap32(0x01000000);\n"
    "    long long w = 0xFFFFFFFFFFLL;\n"
    "    int g = __builtin_popcountll(w);\n"
    "    return a + b + c + d + (int)e + g - 39;\n"
    "}\n"
)

ATOMICS_PROGRAM = (
    "int main(void) {\n"
    "    int x = 0;\n"
    "    __builtin_atomic_store(&x, 20);\n"
    "    int y = __builtin_atomic_load(&x);\n"
    "    int old = __builtin_atomic_exchange(&x, 7);\n"
    "    int expected = 7;\n"
    "    int ok = __builtin_compare_and_swap(&x, &expected, 15);\n"
    "    return y + old + x + (ok ? 0 : 100) - 13;\n"
    "}\n"
)

COMPUTED_GOTO_PROGRAM = (
    "int main(void) {\n"
    "    void *tab[2];\n"
    "    tab[0] = &&one;\n"
    "    tab[1] = &&two;\n"
    "    int acc = 0;\n"
    "    int i = 0;\n"
    "    goto *tab[i];\n"
    "one:\n"
    "    acc += 20;\n"
    "    goto *tab[1];\n"
    "two:\n"
    "    acc += 22;\n"
    "    return acc;\n"
    "}\n"
)

COMPLEX_PROGRAM = (
    "#include <complex.h>\n"
    "int main(void) {\n"
    "    double complex z = __cccc_cmplx(20.0, 22.0);\n"
    "    double complex c = conj(z);\n"
    "    return (int)creal(z) + (int)cimag(z) - (int)cimag(c) - 22;\n"
    "}\n"
)

CONVERTVECTOR_PROGRAM = (
    "typedef int v4i __attribute__((vector_size(16)));\n"
    "typedef float v4f __attribute__((vector_size(16)));\n"
    "int main(void) {\n"
    "    v4i a = {10, 11, 10, 11};\n"
    "    v4f b = __builtin_convertvector(a, v4f);\n"
    "    return (int)(b[0] + b[1] + b[2] + b[3]);\n"
    "}\n"
)

# __builtin_return_address is deliberately not value-asserted: the VM returns
# a bytecode pc and the host returns a real return address. Only that it is
# reachable, compiles, and does not disturb the result is checked.
ADDR_BUILTINS_PROGRAM = (
    "int trapper(int x) {\n"
    "    if (x > 0)\n"
    "        return 42;\n"
    "    __builtin_unreachable();\n"
    "}\n"
    "int main(void) {\n"
    "    void *fp = __builtin_frame_address(0);\n"
    "    void *ra = __builtin_return_address(0);\n"
    "    (void)ra;\n"
    "    return fp ? trapper(1) : 1;\n"
    "}\n"
)

ASM_PROGRAM = (
    "int main(void) {\n"
    "    asm(\"nop\");\n"
    "    return 42;\n"
    "}\n"
)

# #968: gen_complex_expr()'s ND_ADD/SUB/MUL/DIV case used to generate the RHS
# operand into *fixed* registers, so a right-nested complex binop (the
# canonical `20.0 + 22.0 * I` literal, parsed as `20.0 + (22.0 * I)`) clobbered
# itself and a function call anywhere in the RHS clobbered the LHS -- all
# native only, since -c=native's host-compiler codegen was correct the whole
# time (that mismatch is how the bug surfaced). Exercises right-nested `*`
# and a funcall in the RHS.
COMPLEX_NESTING_PROGRAM = (
    "#include <complex.h>\n"
    "static double helper(void) { return 7.0; }\n"
    "int main(void) {\n"
    "    double complex z = 20.0 + 22.0 * I;\n"
    "    if (creal(z) != 20.0 || cimag(z) != 22.0) return 1;\n"
    "    double complex a = __cccc_cmplx(1.0, 2.0);\n"
    "    double complex b = __cccc_cmplx(3.0, 4.0);\n"
    "    double complex c = __cccc_cmplx(5.0, 6.0);\n"
    "    double complex mul_r = a * (b * c);\n"
    "    if (creal(mul_r) != -85.0 || cimag(mul_r) != 20.0) return 2;\n"
    "    double complex m = a * (2.0 + helper());\n"
    "    if (creal(m) != 9.0 || cimag(m) != 18.0) return 3;\n"
    "    return 42;\n"
    "}\n"
)

# #964: ND_VLA_PTR had no serializer case at all (`/* unsupported expr kind
# 41 */`), and closing that gap surfaced three more defects: the VLA
# declaration was hoisted above the variable its length reads, the
# subscript lowering added a pointer to a pointer instead of a byte offset,
# and a VLA declared inside a for-loop body's own block needed its wrapping
# ND_BLOCK left unbraced so the declaration stays in scope for the rest of
# the function. Covers a plain VLA, a `int n=..., v[n];` combined
# declaration (one ND_BLOCK holding both), and a VLA re-declared each
# iteration of a loop.
VLA_PROGRAM = (
    "int main(void) {\n"
    "    int n = 4;\n"
    "    int v[n];\n"
    "    v[0] = 10;\n"
    "    v[n - 1] = 21;\n"
    "    int total = v[0] + v[3];\n"
    "\n"
    "    int m = 3, w[m];\n"
    "    w[0] = 5;\n"
    "    total += w[0];\n"
    "\n"
    "    for (int i = 1; i <= 3; i++) {\n"
    "        int len = i;\n"
    "        int loopvla[len];\n"
    "        loopvla[0] = i;\n"
    "        total += loopvla[0];\n"
    "    }\n"
    "\n"
    "    return total;\n"
    "}\n"
)

# #971: subscripting a multi-dimensional VLA used to SIGSEGV in the VM (an
# inner ND_DEREF yielding a VLA-typed row loaded through the row address
# instead of leaving it alone) and mis-serialized the pointer-to-VLA row type
# as `int *[m]` instead of `int (*)[m]` (invalid C, fails to compile). Both
# fixed; this exercises 2-D subscript, a decayed row pointer, and 3-D
# subscript in one program.
VLA_MULTIDIM_PROGRAM = (
    "int main(void) {\n"
    "    int n = 2, m = 3;\n"
    "    int v[n][m];\n"
    "    v[0][0] = 1; v[0][1] = 2; v[0][2] = 3;\n"
    "    v[1][0] = 4; v[1][1] = 5; v[1][2] = 6;\n"
    "    int total = v[0][0] + v[0][1] + v[0][2] +\n"
    "                v[1][0] + v[1][1] + v[1][2];\n"
    "\n"
    "    int *row = v[1];\n"
    "    row[2] = 42;\n"
    "    total += v[1][2] - 6; // now 42 instead of 6\n"
    "\n"
    "    int k = 4;\n"
    "    int w[n][m][k];\n"
    "    w[1][2][3] = 42;\n"
    "    total = total - total + w[1][2][3]; // isolate the 3-D result\n"
    "\n"
    "    return total;\n"
    "}\n"
)

# #973: `&v` on a VLA local used to yield the frame slot holding the alloca'd
# data pointer instead of the data address itself -- `(*p)[i]` through
# `int (*p)[n] = &v` read garbage. Fixed in codegen.c's gen_expr ND_ADDR case
# (route through gen_expr instead of gen_addr for a TY_VLA operand); the
# type (`int (*)[n]`, NOT decayed to `int *` the way TY_ARRAY is) was already
# correct and unchanged. Exercises the value round trip and the row-stride
# type check together.
VLA_ADDR_PROGRAM = (
    "int main(void) {\n"
    "    int n = 3;\n"
    "    int v[n];\n"
    "    v[0] = 7; v[1] = 8; v[2] = 9;\n"
    "    int (*p)[n] = &v;\n"
    "    (*p)[1] = 42;\n"
    "    int total = v[1]; // must see the write through p\n"
    "\n"
    "    long stride = (long)((char *)(&v + 1) - (char *)&v);\n"
    "    total = total - total + (stride == (long)(n * sizeof(int)) ? 42 : 1);\n"
    "\n"
    "    return total;\n"
    "}\n"
)

# #976: `&v[1] - &v[0]` on a 2-D VLA used to divide the byte difference by
# TY_VLA's placeholder pointer-sized `size` (8) instead of the row's runtime
# vla_size -- worse, the "VLA - num" arm intercepted the ptr-ptr case
# unconditionally (it didn't check whether rhs was itself a pointer), so the
# fix to the divisor alone was dead code until that guard was added too.
# Exercises both directions: only the negative one catches the ty_ulong
# division trap (an unsigned divisor promotes the whole division, turning -1
# into a huge positive garbage value).
#
# #977: a multi-dimensional VLA brace initializer (`int v[n][m] =
# {{1,2},{3,4}}`) used to be silently dropped -- create_lvar_init had no
# TY_VLA case for a nested row's brace group, so every element stayed 0.
VLA_ROW_SUB_AND_INIT_PROGRAM = (
    "int main(void) {\n"
    "    int n = 2, m = 3;\n"
    "    int v[n][m] = {{1, 2, 3}, {4, 5, 6}};\n"
    "    int total = v[0][0] + v[0][1] + v[0][2]"
    " + v[1][0] + v[1][1] + v[1][2]; // 21\n"
    "\n"
    "    long d1 = &v[1] - &v[0];\n"
    "    long d0 = &v[0] - &v[1];\n"
    "    total = total - total + (total == 21 && d1 == 1 && d0 == -1"
    " ? 42 : 1);\n"
    "\n"
    "    return total;\n"
    "}\n"
)

# #964: ND_OVERFLOW_ARITH had no serializer case (`/* unsupported expr kind
# 55 */`) -- val (0/1/2) selects add/sub/mul, lowering directly onto the
# same-named clang/gcc builtin. Exercises all three ops, both an overflowing
# and a non-overflowing case.
OVERFLOW_PROGRAM = (
    "int main(void) {\n"
    "    int r;\n"
    "    int ok = __builtin_add_overflow(2, 3, &r);\n"
    "    if (ok || r != 5) return 1;\n"
    "    ok = __builtin_sub_overflow(10, 3, &r);\n"
    "    if (ok || r != 7) return 2;\n"
    "    ok = __builtin_mul_overflow(6, 7, &r);\n"
    "    if (ok || r != 42) return 3;\n"
    "    ok = __builtin_add_overflow(2147483647, 1, &r);\n"
    "    if (!ok) return 4;\n"
    "    ok = __builtin_mul_overflow(1000000000, 10, &r);\n"
    "    if (!ok) return 5;\n"
    "    return 42;\n"
    "}\n"
)


def case_bitops_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  39: -c=native, bit-manipulation builtins keep their width "
          "(popcount/parity encode width 0, so the ll variant comes from the "
          "argument type) (#963)")
    return _vm_and_native_run_case(cccc, tmp, "bitops_963", BITOPS_PROGRAM)


def case_atomics_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  40: -c=native, atomic load/store/exchange/CAS run natively -- "
          "an atomic store in statement position used to serialize to a null "
          "statement and silently vanish (#963)")
    return _vm_and_native_run_case(cccc, tmp, "atomics_963", ATOMICS_PROGRAM)


def case_computed_goto_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  41: -c=native, labels-as-values and `goto *ptr` round-trip (#963)")
    return _vm_and_native_run_case(cccc, tmp, "computed_goto_963", COMPUTED_GOTO_PROGRAM)


def case_complex_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  42: -c=native, _Complex construction and creal/cimag/conj "
          "round-trip (#963)")
    return _vm_and_native_run_case(cccc, tmp, "complex_963", COMPLEX_PROGRAM)


def case_convertvector_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  43: -c=native, __builtin_convertvector round-trips with an "
          "attributed vector type name (#963)")
    return _vm_and_native_run_case(cccc, tmp, "convertvector_963", CONVERTVECTOR_PROGRAM)


def case_addr_builtins_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  44: -c=native, frame/return address and the trap builtins "
          "compile and run (#963)")
    return _vm_and_native_run_case(cccc, tmp, "addr_builtins_963", ADDR_BUILTINS_PROGRAM)


def case_asm_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  45: -c=native, asm(...) is emitted verbatim and handed to the "
          "host compiler (#963)")
    return _vm_and_native_run_case(cccc, tmp, "asm_963", ASM_PROGRAM)


def case_complex_nesting_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  46: right-nested complex binops and a funcall in the RHS match "
          "-c=native (#968)")
    return _vm_and_native_run_case(cccc, tmp, "complex_nesting_968",
                                    COMPLEX_NESTING_PROGRAM)


def case_vla_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  47: -c=native, a 1-D VLA (plain, combined `n=..,v[n]` "
          "declaration, and one re-declared per loop iteration) round-trips "
          "as a real C VLA (#964)")
    return _vm_and_native_run_case(cccc, tmp, "vla_964", VLA_PROGRAM)


def case_overflow_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  48: -c=native, __builtin_{add,sub,mul}_overflow round-trip, "
          "overflowing and not (#964)")
    return _vm_and_native_run_case(cccc, tmp, "overflow_964", OVERFLOW_PROGRAM)


def case_vla_multidim_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  49: -c=native, a multi-dimensional VLA (2-D subscript, a "
          "decayed row pointer, and 3-D subscript) round-trips as real C "
          "and no longer SIGSEGVs in the VM (#971)")
    return _vm_and_native_run_case(cccc, tmp, "vla_multidim_971",
                                    VLA_MULTIDIM_PROGRAM)


def case_vla_addr_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  50: -c=native, `&v` on a VLA local yields the array's data "
          "address (not the frame slot holding the alloca'd pointer) and "
          "keeps its non-decayed `int (*)[n]` row-stride type (#973)")
    return _vm_and_native_run_case(cccc, tmp, "vla_addr_973",
                                    VLA_ADDR_PROGRAM)


def case_vla_row_sub_and_init_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  51: -c=native, pointer-to-VLA-row subtraction divides by the "
          "row's runtime size (both directions) and a multi-dimensional VLA "
          "brace initializer round-trips as real C (#976/#977)")
    return _vm_and_native_run_case(cccc, tmp, "vla_row_sub_and_init_976_977",
                                    VLA_ROW_SUB_AND_INIT_PROGRAM)


# #982 (defect D): a PARTIAL multi-dimensional VLA brace initializer (fewer
# rows than the array's outer dimension) left the omitted row relying on the
# fresh alloca block already being zero -- true at the VM's default safety
# level, but not under -2/-3's memory poisoning (0xCD fill). Fixed by
# prepending an ND_MEMZERO ahead of the VLA init in var_definition()
# (src/parse.c), read back through a runtime `ty->vla_size` byte count
# (src/codegen.c). This case isn't about poisoning (the native path has no
# such thing) -- it instead checks that a real C compiler agrees an omitted
# row must read back as zero, and that the new ND_COMMA(ND_MEMZERO, init)
# AST shape serializes correctly through -c=native at all (case 51 above
# only covers a FULL initializer, never exercising the new memzero node).
VLA_PARTIAL_INIT_PROGRAM = (
    "int main(void) {\n"
    "    int n = 2, m = 2;\n"
    "    int v[n][m] = {{1, 2}};   // second row omitted\n"
    "    int total = v[0][0] + v[0][1] * 10"
    " + (v[1][0] == 0 && v[1][1] == 0 ? 0 : 100);\n"
    "    return total == 21 ? 42 : 1;\n"
    "}\n"
)


def case_vla_partial_init_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  52: -c=native, a partial multi-dimensional VLA brace "
          "initializer zero-fills its omitted row (#982)")
    return _vm_and_native_run_case(cccc, tmp, "vla_partial_init_982",
                                    VLA_PARTIAL_INIT_PROGRAM)


# #965: ND_BLOCK_LITERAL/ND_BLOCK_CALL/TY_BLOCK had no serializer case at
# all -- `-m`/`-c=native` printed `/* unsupported expr kind 49/50 */` and
# `/* unknown type */` in their place, and the lifted function reached the
# output unrenamed (a `.L..N` VM-internal label, not a legal C identifier).
# A `-m` shape assertion alone can't see this failure mode (an unhandled
# node kind in statement position is a silently-valid null statement) -- see
# tests/test_serialize_expr_vla.c's own comment for the general rule this
# repeats -- so each of the four cases below is a VM-42-then-native-42
# round trip, covering the capture matrix COVERAGE.md:197 claims the VM
# supports: plain by-value capture, __block mutation, transitive/nested
# capture plus Block_copy escape, and a __block aggregate's partial brace
# initializer (the one case that exercises ND_MEMZERO's new is_block_var
# arm, which a `-m` shape assertion can't distinguish from the pre-#965
# wrong-8-bytes shape either).
BLOCK_CAPTURE_PROGRAM = (
    "int main(void) {\n"
    "    int a = 21;\n"
    "    int (^add)(int) = ^(int x) { return x + a; };\n"
    "    int r = add(21);\n"
    "    return r == 42 ? 42 : 1;\n"
    "}\n"
)


def case_block_capture_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  53: -c=native, a block literal with a by-value capture and a "
          "block call round-trip as real C (#965)")
    return _vm_and_native_run_case(cccc, tmp, "block_capture_965",
                                    BLOCK_CAPTURE_PROGRAM)


BLOCK_MUTABLE_PROGRAM = (
    "int main(void) {\n"
    "    __block int counter = 0;\n"
    "    void (^inc)(void) = ^{ counter++; };\n"
    "    inc();\n"
    "    inc();\n"
    "    return counter == 2 ? 42 : 1;\n"
    "}\n"
)


def case_block_mutable_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  54: -c=native, a __block variable shared and mutated by a "
          "block round-trips as real C (#965)")
    return _vm_and_native_run_case(cccc, tmp, "block_mutable_965",
                                    BLOCK_MUTABLE_PROGRAM)


BLOCK_NESTED_COPY_PROGRAM = (
    "#include <stdlib.h>\n"
    "typedef int (^IntBlock)(void);\n"
    "IntBlock make_adder(int x) {\n"
    "    IntBlock inner = ^{ return x + 1; };\n"
    "    return Block_copy(inner);\n"
    "}\n"
    "int main(void) {\n"
    "    IntBlock a = make_adder(41);\n"
    "    int ra = a();\n"
    "    Block_release(a);\n"
    "    int outer = 10;\n"
    "    int (^level1)(void) = ^{\n"
    "        int mid = outer + 1;\n"
    "        int (^level2)(void) = ^{ return mid + outer; };\n"
    "        return level2();\n"
    "    };\n"
    "    int r2 = level1();\n"
    "    return (ra == 42 && r2 == 21) ? 42 : 1;\n"
    "}\n"
)


def case_block_nested_copy_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  55: -c=native, transitive/nested block capture and an "
          "escaping Block_copy round-trip as real C (#965)")
    return _vm_and_native_run_case(cccc, tmp, "block_nested_copy_965",
                                    BLOCK_NESTED_COPY_PROGRAM)


BLOCK_PARTIAL_INIT_PROGRAM = (
    "int main(void) {\n"
    "    __block int x[4] = {1};\n"
    "    int (^sum)(void) = ^{\n"
    "        return x[0] + x[1] + x[2] + x[3];\n"
    "    };\n"
    "    return sum() == 1 ? 42 : 1;\n"
    "}\n"
)


def case_block_partial_init_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  56: -c=native, a __block aggregate's partial brace "
          "initializer, read from inside a block, round-trips as real C "
          "(#965) -- exercises ND_MEMZERO's is_block_var arm")
    return _vm_and_native_run_case(cccc, tmp, "block_partial_init_965",
                                    BLOCK_PARTIAL_INIT_PROGRAM)


BLOCK_LOCAL_TYPE_HOIST_PROGRAM = (
    "int main(void) {\n"
    "    struct Q { int y; };\n"
    "    struct P { struct Q q; };\n"
    "    struct P p = {{1}};\n"
    "    struct P *pp = &p;\n"
    "    struct { int z; } t = {2};\n"
    "\n"
    "    int (^b1)(void) = ^{ return p.q.y; };\n"
    "    int (^b2)(void) = ^{ return pp->q.y; };\n"
    "    int (^b3)(void) = ^{ return t.z; };\n"
    "\n"
    "    return b1() + b2() + b3() + 38;\n"
    "}\n"
)


def case_block_local_type_hoist_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  57: -c=native, a block capture whose own struct/union type is "
          "declared inside a function round-trips as real C via file-scope "
          "hoisting (#989) -- by-value capture of a tagged local struct, "
          "pointer-to-local-struct capture, a tagless local aggregate, and "
          "a nested local type (transitive hoisting), all in one program")
    return _vm_and_native_run_case(cccc, tmp, "block_local_type_hoist_989",
                                    BLOCK_LOCAL_TYPE_HOIST_PROGRAM)


# #990: Block_release's builtin_free fallback has no obj->tok, so it was
# silently dropped by the prototype pass's from_primary filter -- the
# generated C called an undeclared free(). Deliberately no <stdlib.h> here,
# or the test proves nothing (the header's own free() prototype would mask
# the gap).
BLOCK_RELEASE_NO_STDLIB_PROGRAM = (
    "typedef int (^IntBlock)(void);\n"
    "IntBlock make_adder(int x) {\n"
    "    IntBlock inner = ^{ return x + 1; };\n"
    "    return Block_copy(inner);\n"
    "}\n"
    "int main(void) {\n"
    "    IntBlock a = make_adder(41);\n"
    "    int r = a();\n"
    "    Block_release(a);\n"
    "    return r == 42 ? 42 : 1;\n"
    "}\n"
)


def case_block_release_no_stdlib_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  58: -c=native, Block_copy/Block_release round-trip without "
          "<stdlib.h> in scope (#990) -- builtin_free's fallback prototype "
          "now gets its own extern declaration")
    return _vm_and_native_run_case(cccc, tmp, "block_release_no_stdlib_990",
                                    BLOCK_RELEASE_NO_STDLIB_PROGRAM)


def case_block_release_with_stdlib_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  59: -c=native positive control, Block_copy/Block_release still "
          "round-trip when <stdlib.h> IS in scope (#990) -- the new extern "
          "free() declaration must not collide with the real one")
    program = "#include <stdlib.h>\n" + BLOCK_RELEASE_NO_STDLIB_PROGRAM
    return _vm_and_native_run_case(cccc, tmp, "block_release_with_stdlib_990",
                                    program)


# #993: a by-value capture of a header-declared type (struct tm) used to be
# serialized ahead of the #include that brings it into scope.
BLOCK_HEADER_TYPE_CAPTURE_PROGRAM = (
    "#include <time.h>\n"
    "int main(void) {\n"
    "    struct tm t;\n"
    "    t.tm_year = 42;\n"
    "    int (^b)(void) = ^{ return t.tm_year; };\n"
    "    return b();\n"
    "}\n"
)


def case_block_header_type_capture_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  60: -c=native, a block's by-value capture of a header-declared "
          "type (struct tm) round-trips as real C (#993), and -m output "
          "places the #include ahead of the env struct that needs it. "
          "Also asserts VM/native equivalence directly (_vm_and_native_run_"
          "case): struct tm is larger than 8 bytes, and until #994 the VM's "
          "own block-capture codegen truncated any by-value aggregate "
          "capture above 8 bytes to a single word, so this case used to be "
          "native-only. #994 fixed the truncation, so the VM side now "
          "returns 42 too and this can assert real equivalence instead")
    src = Path(tmp) / "block_header_type_capture_993.c"
    write(src, BLOCK_HEADER_TYPE_CAPTURE_PROGRAM)
    if not _vm_and_native_run_case(cccc, tmp, "block_header_type_capture_993",
                                    BLOCK_HEADER_TYPE_CAPTURE_PROGRAM):
        return False
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    inc_idx = out.find("#include <time.h>")
    env_idx = out.find("struct __cccc_block_env_")
    if inc_idx < 0 or env_idx < 0 or inc_idx > env_idx:
        print(f"    FAIL: -m output does not place #include <time.h> before "
              f"the env struct (inc_idx={inc_idx}, env_idx={env_idx})\n"
              f"    {out}")
        return False
    print("    ok")
    return True


# #993 (second mechanism): a capture's type from a cccc-only-routed include
# (whose own #include is deliberately never re-emitted, #896) reaches the
# output via serialize_type_defs_for_owner instead of the #include replay --
# moving only the replay would not have fixed this shape.
BLOCK_ROUTED_INCLUDE_LIB = (
    "#include @comptime <dlfcn.h>\n"
    "\n"
    "[[cccc::comptime]]\n"
    "void gen(void) {\n"
    "}\n"
    "gen();\n"
    "\n"
    "struct Pt { int x; };\n"
)

BLOCK_ROUTED_INCLUDE_MAIN = (
    '#include "routedlib_993.c"\n'
    "int main(void) {\n"
    "    struct Pt p;\n"
    "    p.x = 42;\n"
    "    int (^b)(void) = ^{ return p.x; };\n"
    "    return b();\n"
    "}\n"
)


def case_block_routed_include_type_capture_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  61: -c=native, a block's by-value capture of a struct declared "
          "in a cccc-only-routed include (#896) still round-trips as real C "
          "(#993) -- the type reaches the output via "
          "serialize_type_defs_for_owner, not the #include replay")
    write(Path(tmp) / "routedlib_993.c", BLOCK_ROUTED_INCLUDE_LIB)
    src = Path(tmp) / "block_routed_include_993.c"
    write(src, BLOCK_ROUTED_INCLUDE_MAIN)
    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False
    out_bin = Path(tmp) / "block_routed_include_993_out"
    compile_result = run([str(cccc), "-c=native", "-o", out_bin.name, src.name], cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native compile exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    def_idx = out.find("struct Pt {")
    env_idx = out.find("struct __cccc_block_env_")
    if def_idx < 0 or env_idx < 0 or def_idx > env_idx:
        print(f"    FAIL: -m output does not place struct Pt's definition "
              f"before the env struct (def_idx={def_idx}, env_idx={env_idx})\n"
              f"    {out}")
        return False
    print("    ok")
    return True


# #990/#993 (no-literal TU): a TU that only uses a block *type* (parameter,
# no literal anywhere) used to skip the whole preamble -- struct
# __cccc_block was never defined, and __cccc_block_copy_impl/free were both
# left undeclared.
BLOCK_NO_LITERAL_PROGRAM = (
    "typedef int (^IntBlock)(void);\n"
    "int consume(IntBlock b) {\n"
    "    IntBlock c = Block_copy(b);\n"
    "    int r = c();\n"
    "    Block_release(c);\n"
    "    return r;\n"
    "}\n"
)


def case_block_no_literal_preamble_m_output(cccc: Path, tmp: str) -> bool:
    print("  62: -m output defines struct __cccc_block and the copy/free "
          "helpers for a TU that uses a block type but declares no block "
          "literal (#990/#993) -- can't run this one (no main), so assert "
          "on -m shape and that a system compiler accepts it")
    src = Path(tmp) / "block_no_literal_990_993.c"
    write(src, BLOCK_NO_LITERAL_PROGRAM)
    result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = result.stdout
    missing = [needle for needle in (
        "struct __cccc_block {",
        "__cccc_block_copy_impl",
        "extern void free(void *);",
    ) if needle not in out]
    if missing:
        print(f"    FAIL: -m output missing {missing}\n    {out}")
        return False
    out_c = Path(tmp) / "block_no_literal_990_993_out.c"
    write(out_c, out)
    cc = os.environ.get("CC", "cc")
    compile_result = run([cc, "-x", "c", "-c", "-o", os.devnull, out_c.name], cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: system compiler rejected -m output "
              f"(exit {compile_result.returncode})\n    {compile_result.stderr}")
        return False
    print("    ok")
    return True


# #994: a block capture of a by-value struct larger than 8 bytes used to be
# silently truncated to its first word by the VM's block-literal codegen --
# the descriptor was a flat one-8-byte-slot-per-capture array regardless of
# the capture's real type. The native serializer already copied such a
# capture correctly (plain struct assignment into a real-typed env struct
# field), so this is a VM-only miscompile; asserting VM/native equivalence
# is exactly what would have caught it.
BLOCK_LARGE_STRUCT_CAPTURE_PROGRAM = (
    "struct BigS { long a; long b; long c; };\n"
    "int main(void) {\n"
    "    struct BigS t;\n"
    "    t.a = 10;\n"
    "    t.b = 20;\n"
    "    t.c = 12;\n"
    "    int (^b)(void) = ^{ return (int)(t.a + t.b + t.c); };\n"
    "    return b();\n"
    "}\n"
)


def case_block_large_struct_capture_round_trip(cccc: Path, tmp: str) -> bool:
    print("  63: a block capture of a by-value struct larger than 8 bytes "
          "(24-byte struct, three long members) round-trips VM 42 -> "
          "native 42 (#994)")
    return _vm_and_native_run_case(cccc, tmp, "block_large_struct_capture_994",
                                    BLOCK_LARGE_STRUCT_CAPTURE_PROGRAM)


MACRO_GENERATED_BLOCK_LOCALS_PROGRAM = (
    "[[cccc::comptime]]\n"
    "void gen(void) {\n"
    "    Obj *fn = MakeFunction(\"use_block\", GetType(\"int\"));\n"
    "    FunctionSetBody(fn, Quote(\n"
    "        \"{ int n = 42; int (^b)(void) = ^{ return n; }; return b(); }\"\n"
    "    ));\n"
    "    PublishNode(fn);\n"
    "}\n"
    "gen();\n"
    "\n"
    "int use_block(void);\n"
    "\n"
    "int main(void) {\n"
    "    return use_block();\n"
    "}\n"
)


def case_macro_generated_block_locals_round_trip(cccc: Path, tmp: str) -> bool:
    print("  64: a capturing block literal inside a MakeFunction()+"
          "FunctionSetBody(fn, Quote(...)) body built without WithFn(fn) "
          "round-trips VM 42 -> native 42 (#996) -- a -m shape assertion "
          "alone can't see the execution-level failure this ticket was "
          "about (every local in the generated body aliased frame offset "
          "0, so the block's own descriptor got clobbered and CALLI jumped "
          "to a raw host address)")
    return _vm_and_native_run_case(cccc, tmp, "macro_generated_block_locals_996",
                                    MACRO_GENERATED_BLOCK_LOCALS_PROGRAM)


# #995: same source as case 64 (MACRO_GENERATED_BLOCK_LOCALS_PROGRAM), but
# this exercises -c=generated specifically -- a real header-comment/-m/
# -c=native shape assertion cannot see this failure mode, since -m/-c=native
# both run the (!generated_only) serializer path, which was never broken.
# main() here is deliberately NOT macro-generated (it's hand-written, same
# as case 64's), so it never reaches -c=generated's output at all -- the
# generated .gen.c is linked against a *separate*, hand-written harness
# object that supplies main() and calls use_block(), the only way to prove
# __cccc_block_0's definition actually reached the linker (a `cc -c`
# compile-only check, as case 38 uses for its own -c=generated assertion,
# would not catch a dangling call to a function that was never emitted at
# all -- that only shows up as an undefined-symbol error at link time).
MACRO_GENERATED_BLOCK_HARNESS = (
    "int use_block(void);\n"
    "int main(void) {\n"
    "    return use_block();\n"
    "}\n"
)


def case_macro_generated_block_generated_output_links(cccc: Path, tmp: str) -> bool:
    print("  65: -c=generated emits a block literal lifted while building a "
          "macro-generated function body (#995) -- block_literal() never "
          "set is_macro_generated on the lifted function, so "
          "cc_record_emit_object never recorded it and it was silently "
          "dropped from -c=generated output while the caller's body still "
          "called it, leaving generated C that fails to link")
    src = Path(tmp) / "macro_generated_block_gen_995.c"
    write(src, MACRO_GENERATED_BLOCK_LOCALS_PROGRAM)
    out_path = Path(tmp) / "macro_generated_block_gen_995.gen.c"
    result = run([str(cccc), "-c=generated", "-o", out_path.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: -c=generated exited {result.returncode}\n    {result.stderr}")
        return False
    out = out_path.read_text()
    if out.count("__cccc_block_0") < 3:
        # forward decl + descriptor reference + definition header, at least
        print(f"    FAIL: -c=generated output does not look like it defines "
              f"__cccc_block_0 (found {out.count('__cccc_block_0')} "
              f"occurrences)\n    {out}")
        return False
    gen_obj = Path(tmp) / "macro_generated_block_gen_995.o"
    cc_result = run(["cc", "-c", out_path.name, "-o", str(gen_obj)], cwd=tmp)
    if cc_result.returncode != 0:
        print(f"    FAIL: host cc rejected the -c=generated output\n"
              f"    {cc_result.stderr}\n    {out}")
        return False
    harness_src = Path(tmp) / "macro_generated_block_harness_995.c"
    write(harness_src, MACRO_GENERATED_BLOCK_HARNESS)
    harness_obj = Path(tmp) / "macro_generated_block_harness_995.o"
    cc_result = run(["cc", "-c", harness_src.name, "-o", str(harness_obj)], cwd=tmp)
    if cc_result.returncode != 0:
        print(f"    FAIL: host cc rejected the harness\n    {cc_result.stderr}")
        return False
    out_bin = Path(tmp) / "macro_generated_block_gen_995_out"
    link_result = run(["cc", "-o", str(out_bin), str(gen_obj), str(harness_obj)], cwd=tmp)
    if link_result.returncode != 0:
        print(f"    FAIL: linking the -c=generated object against a "
              f"hand-written main() that calls use_block() failed -- "
              f"__cccc_block_0 was likely dropped from the -c=generated "
              f"output\n    {link_result.stderr}")
        return False
    run_result = run([str(out_bin)], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


TIME_H_COMPTIME_PROGRAM = (
    "[[cccc::comptime]]\n"
    "void gen(void) {\n"
    "    Obj *fn = MakeFunction(\"use_it\", GetType(\"int\"));\n"
    "    FunctionSetBody(fn, Quote(\"{ struct tm t; t.tm_year = 42; "
    "return t.tm_year; }\"));\n"
    "    PublishNode(fn);\n"
    "}\n"
    "gen();\n"
    "int use_it(void);\n"
    "int main(void) { return use_it(); }\n"
)


def case_generated_embedded_header_no_duplicate(cccc: Path, tmp: str) -> bool:
    print("  66: -c=generated output for a standard header served from the "
          "embedded src/std.c table (no on-disk ./include, since this runs "
          "from a temp cwd) has no duplicate struct tm when the header is "
          "both @comptime-routed and plainly #included (#998)")
    src = Path(tmp) / "time_h_998a.c"
    write(src, "#include @comptime <time.h>\n#include <time.h>\n" +
          TIME_H_COMPTIME_PROGRAM)
    out_path = Path(tmp) / "time_h_998a.gen.c"
    result = run([str(cccc), "-c=generated", "-o", out_path.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: -c=generated exited {result.returncode}\n    {result.stderr}")
        return False
    out = out_path.read_text()
    if "struct tm {" in out:
        print(f"    FAIL: -c=generated output re-derives struct tm on top "
              f"of the replayed #include <time.h>\n    {out}")
        return False
    if "#include <time.h>" not in out or "use_it" not in out:
        print(f"    FAIL: -c=generated output missing expected content\n    {out}")
        return False
    obj = Path(tmp) / "time_h_998a.o"
    cc_result = subprocess.run(["cc", "-c", out_path.name, "-o", str(obj)],
                                capture_output=True, text=True, cwd=tmp)
    if cc_result.returncode != 0:
        print(f"    FAIL: host cc rejected the -c=generated output (likely "
              f"a struct tm redefinition)\n    {cc_result.stderr}\n    {out}")
        return False
    print("    ok")
    return True


def case_generated_embedded_header_comptime_only_still_derives(cccc: Path, tmp: str) -> bool:
    print("  67: -c=generated output for a standard header reached only via "
          "#include @comptime (embedded resolution) still derives its "
          "definition -- guards #998's fix against over-suppression")
    src = Path(tmp) / "time_h_998b.c"
    write(src, "#include @comptime <time.h>\n" + TIME_H_COMPTIME_PROGRAM)
    out_path = Path(tmp) / "time_h_998b.gen.c"
    result = run([str(cccc), "-c=generated", "-o", out_path.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: -c=generated exited {result.returncode}\n    {result.stderr}")
        return False
    out = out_path.read_text()
    if "struct tm {" not in out:
        print(f"    FAIL: -c=generated output dropped struct tm's "
              f"definition -- its @comptime-routed #include is never "
              f"captured, so nothing else supplies it\n    {out}")
        return False
    if "#include <time.h>" in out:
        print(f"    FAIL: -c=generated output leaked the @comptime-routed "
              f"#include verbatim\n    {out}")
        return False
    obj = Path(tmp) / "time_h_998b.o"
    cc_result = subprocess.run(["cc", "-c", out_path.name, "-o", str(obj)],
                                capture_output=True, text=True, cwd=tmp)
    if cc_result.returncode != 0:
        print(f"    FAIL: host cc rejected the -c=generated output\n    {cc_result.stderr}\n    {out}")
        return False
    print("    ok")
    return True


def case_native_embedded_header_include_not_suppressed(cccc: Path, tmp: str) -> bool:
    print("  68: a plain #include <stdio.h> resolved from the embedded "
          "src/std.c table still appears in -m output and still builds/runs "
          "under -c=native (#998 regression guard: the new emit_include_paths "
          "registration for an embedded header must not feed "
          "cc_file_is_cccc_only and suppress a legitimate #include)")
    src = Path(tmp) / "stdio_998.c"
    write(src, '#include <stdio.h>\n'
                'int main(void) { printf("hi\\n"); return 42; }\n')
    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if m_result.returncode != 0:
        print(f"    FAIL: -m exited {m_result.returncode}\n    {m_result.stderr}")
        return False
    if "#include <stdio.h>" not in m_result.stdout:
        print(f"    FAIL: -m output dropped #include <stdio.h>\n    {m_result.stdout}")
        return False
    out_bin = Path(tmp) / "stdio_998_out"
    result = run([str(cccc), "-c=native", "-o", out_bin.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: -c=native exited {result.returncode}\n    {result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


DANDY_PATTERN_HEADER = (
    # No #pragma once / #ifndef guard, deliberately: this case is about a
    # `static` definition and a scalar typedef reached by two translation
    # units through an ordinary #include, independent of the separate,
    # out-of-scope bug where pragma_once/include_guard state persists
    # across every TU one cccc invocation compiles together (a guarded
    # header would silently mask the very thing this case checks -- see
    # tests/fixtures/header_static_skip_999.h's identical note).
    "typedef unsigned long DPValue;\n"
    "static inline DPValue dp_box(unsigned long x) { return x; }\n"
    "typedef struct { int (*open)(void); int (*close)(void); } DPVT;\n"
)

DANDY_PATTERN_A = (
    '#include "dandy_pattern_999.h"\n'
    "static int dp_open(void);\n"
    "static int dp_close(void);\n"
    "static const DPVT kVT = { .open = dp_open, .close = dp_close };\n"
    "static int dp_open(void) { return (int)dp_box(40); }\n"
    "static int dp_close(void) { return (int)dp_box(2); }\n"
    "int dp_call_a(void) { return kVT.open() + kVT.close(); }\n"
)

DANDY_PATTERN_B = (
    '#include "dandy_pattern_999.h"\n'
    "int dp_call_a(void);\n"
    "DPValue dp_helper(DPValue v) { return dp_box(v); }\n"
    "int main(void) {\n"
    "    return dp_call_a() + (dp_helper(21) == 21 ? 0 : 999);\n"
    "}\n"
)


def case_dandy_vtable_pattern_multi_tu(cccc: Path, tmp: str) -> bool:
    print("  69: dandy's collector-vtable pattern (#999) -- a static const "
          "vtable of pointers to later-defined file statics, plus a shared "
          "header (no include guard) whose static inline accessor over a "
          "scalar typedef is reached from two translation units -- compiles "
          "and links under -c=native (not just a -m shape assertion: a "
          "dropped/duplicated definition or a lost typedef spelling is a "
          "*link/compile* failure, which tests/test_serialize_static_fn_"
          "ptr_init.c, tests/test_serialize_header_static_skip.c and "
          "tests/test_serialize_typedef_spelling.c individually can't see). "
          "Asserts VM 42 -> native 42")
    write(Path(tmp) / "dandy_pattern_999.h", DANDY_PATTERN_HEADER)
    a_src = Path(tmp) / "dandy_pattern_999_a.c"
    b_src = Path(tmp) / "dandy_pattern_999_b.c"
    write(a_src, DANDY_PATTERN_A)
    write(b_src, DANDY_PATTERN_B)

    vm_result = run([str(cccc), a_src.name, b_src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    out_bin = Path(tmp) / "dandy_pattern_999_out"
    compile_result = run(
        [str(cccc), "-c=native", "-o", out_bin.name, a_src.name, b_src.name],
        cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_polyfill_header_embedded_round_trip(cccc: Path, tmp: str) -> bool:
    print("  70: -c=native, #include <stdbit.h> (a cccc-owned polyfill with "
          "no guaranteed real system counterpart) resolved from the "
          "embedded src/std.c table -- since this runs from a temp cwd, "
          "there is no on-disk ./include -- rounds trip VM 42 -> native 42 "
          "instead of the host compiler failing with 'stdbit.h' file not "
          "found (#1003). tests/test_serialize_polyfill_header_not_"
          "replayed.c covers the -m shape and the on-disk resolution "
          "branch (tools/tests.py always passes -I./include); this case "
          "is the one that actually exercises the embedded branch, per "
          "the #998 lesson that the two resolution paths need separate "
          "coverage")
    return _vm_and_native_run_case(
        cccc, tmp, "polyfill_header_1003",
        "#include <stdbit.h>\n"
        "int main(void) { return (int)stdc_leading_zeros_ui(1u) + 11; }\n")


def case_static_name_collision_multi_tu(cccc: Path, tmp: str) -> bool:
    print("  71: -c=native, two different .c inputs each independently "
          "defining `static int collide_1002(void)` with no shared header "
          "-- cc_link_progs deliberately never canonicalizes static Objs "
          "across TUs (#957), so both reached -c=native output unrenamed "
          "and collided ('redefinition of collide_1002'). "
          "rename_colliding_static_names() (#1002) renames every same-"
          "named static Obj but the first when more than one distinct "
          "file defines it. Asserts VM 42 -> native 42 -- a link/compile "
          "failure tests/test_serialize_static_name_collision.c's -m "
          "shape assertion alone can't see")
    a_src = Path(tmp) / "collision_1002_a.c"
    b_src = Path(tmp) / "collision_1002_b.c"
    write(a_src,
          "static int collide_1002(void) { return 20; }\n"
          "int collide_1002_call_a(void) { return collide_1002(); }\n")
    write(b_src,
          "int collide_1002_call_a(void);\n"
          "static int collide_1002(void) { return 22; }\n"
          "int main(void) { return collide_1002_call_a() + collide_1002(); }\n")

    vm_result = run([str(cccc), a_src.name, b_src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    out_bin = Path(tmp) / "collision_1002_out"
    compile_result = run(
        [str(cccc), "-c=native", "-o", out_bin.name, a_src.name, b_src.name],
        cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_header_static_fn_mixed_path_spelling_1032(cccc: Path, tmp: str) -> bool:
    print("  80: -c=native, a header-defined static inline function shared "
          "by two TUs, invoked with one input file as an absolute path and "
          "the other relative (tools/testing/native.py's own invocation "
          "shape) -- rename_colliding_static_names() compared File.name by "
          "raw strcmp, so the two TUs' own #include resolution of the "
          "identical on-disk header (dirname(including-file) + the quoted "
          "spelling) recorded two differently-spelled paths for it, wrongly "
          "treating a shared header-supplied function as a cross-file name "
          "collision. The rename this produced was worse than a spurious "
          "warning: it renamed the function's *call sites* (every use "
          "resolves through the Obj, so a rename is 'free') while the "
          "function's own definition is never re-emitted at all -- it "
          "reaches the output solely via the replayed #include, still under "
          "its original name -- so the renamed call sites referenced a "
          "symbol nothing declares. files_are_same() (#1032) falls back to "
          "realpath() before deciding two File.name spellings differ. "
          "Asserts VM 42 -> native 42, absolute+relative order -- a host "
          "'implicit function declaration' compile failure no -m shape "
          "assertion alone can see")
    hdr = Path(tmp) / "shared_static_1032.h"
    write(hdr,
          "#ifndef SHARED_STATIC_1032_H\n#define SHARED_STATIC_1032_H\n"
          "static inline int box_1032(int x) { return x; }\n#endif\n")
    a_src = Path(tmp) / "static_1032_a.c"
    b_src = Path(tmp) / "static_1032_b.c"
    write(a_src,
          "#include \"shared_static_1032.h\"\n"
          "int use_a_1032(void) { return box_1032(20); }\n")
    write(b_src,
          "#include \"shared_static_1032.h\"\n"
          "int use_a_1032(void);\n"
          "int main(void) { return use_a_1032() + box_1032(22); }\n")

    vm_result = run([str(cccc), a_src.name, b_src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    out_bin = Path(tmp) / "static_1032_out"
    # Deliberately mixed path styles: a_src absolute, b_src relative --
    # exactly what tripped the bug (tools/testing/native.py passes the
    # discovered test file absolute and its CCCC_FLAGS-named sibling
    # relative).
    compile_result = run(
        [str(cccc), "-c=native", "-o", out_bin.name, str(a_src), b_src.name],
        cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_switch_break_continue_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  72: -c=native, break/continue (which used to serialize to the "
          "literal text 'goto (null);', a host compile error) and a real "
          "switch body -- multi-statement cases, fallthrough, a `default:` "
          "that is not last in source order, a GNU case range, a `break` "
          "inside a switch inside a loop, and a `continue` that must skip "
          "over an enclosing switch frame to reach the loop -- round-trip "
          "VM 42 -> native 42 (#1005). A -m shape assertion alone can't "
          "see the switch-body drop returning a wrong answer; only "
          "compiling and running the native output can")
    program = (
        "int main(void) {\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < 6; i++) {\n"
        "        switch (i) {\n"
        "            default:\n"
        "                total += 1;\n"
        "                break;\n"
        "            case 1 ... 3:\n"
        "                total += i;\n"
        "                continue;\n"
        "            case 5:\n"
        "                total += 100;\n"
        "                break;\n"
        "        }\n"
        "        total += 1000;\n" # reached for default(0,4) and case 5 only
        "    }\n"
        "    // i=0: default, total+=1+1000=1001\n"
        "    // i=1..3: case range, total += 1+2+3=6, no +1000 (continue)\n"
        "    // i=4: default, total+=1+1000=1001\n"
        "    // i=5: case 5, total+=100+1000=1100\n"
        "    // total = 1001+6+1001+1100 = 3108\n"
        "    if (total != 3108) return total & 0xff;\n"
        "    int r = 0;\n"
        "    for (int j = 0; j < 5; j++) {\n"
        "        if (j == 3) break;\n"
        "        switch (j) {\n"
        "            case 0: r += 1;\n"
        "            case 1: r += 10; break;\n"
        "            case 2: r += 100;\n"
        "        }\n"
        "    }\n"
        "    // j=0: falls through 0->1, r=11; j=1: r=21; j=2: r=121; j=3: break\n"
        "    return r == 121 ? 42 : r;\n"
        "}\n"
    )
    return _vm_and_native_run_case(cccc, tmp, "switch_break_continue_1005", program)


def case_multi_tu_typedef_and_includes_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  73: -c=native, a non-primary translation unit's own file-scope "
          "typedef and #include (the ticket's own two-file repro: a "
          "`typedef enum` and <stdlib.h>/<stdio.h> written in the second "
          ".c on the command line) both reach the output, instead of being "
          "silently dropped ('unknown type name'/undeclared malloc/free/"
          "puts) because record_type_name() and the preprocessor's "
          "auto-capture gate both keyed off vm->compiler.primary_file, "
          "which only ever names input_files[0] (#1006). This is the "
          "load-bearing proof: the failure is a host compile/link failure "
          "no -m shape assertion alone can see")
    tu1_src = Path(tmp) / "multi_tu_1006_tu1.c"
    tu2_src = Path(tmp) / "multi_tu_1006_tu2.c"
    write(tu1_src, "int multi_tu_1006_other(void) { return 1; }\n")
    write(tu2_src,
          "#include <stdlib.h>\n"
          "#include <stdio.h>\n"
          "typedef enum { MULTI_TU_1006_A, MULTI_TU_1006_B } MultiTu1006Thing;\n"
          "static MultiTu1006Thing multi_tu_1006_pick(const char *s) {\n"
          "    (void)s;\n"
          "    return MULTI_TU_1006_B;\n"
          "}\n"
          "int multi_tu_1006_other(void);\n"
          "int main(void) {\n"
          "    MultiTu1006Thing t = multi_tu_1006_pick(\"x\");\n"
          "    void *p = malloc(8);\n"
          "    free(p);\n"
          "    puts(t == MULTI_TU_1006_B ? \"ok\" : \"no\");\n"
          "    return multi_tu_1006_other() + (t == MULTI_TU_1006_B ? 41 : 0);\n"
          "}\n")

    vm_result = run([str(cccc), tu1_src.name, tu2_src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    out_bin = Path(tmp) / "multi_tu_1006_out"
    compile_result = run(
        [str(cccc), "-c=native", "-o", out_bin.name, tu1_src.name, tu2_src.name],
        cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


OPAQUE_HANDLE_1010_HEADER = (
    "#pragma once\n"
    "typedef struct DyAtoms1010Smoke DyAtoms1010Smoke;\n"
    "DyAtoms1010Smoke *make_atoms_1010_smoke(void);\n"
    "int get_x_1010_smoke(DyAtoms1010Smoke *t);\n"
)

OPAQUE_HANDLE_1010_DEF = (
    '#include "opaque_handle_1010_smoke.h"\n'
    "struct DyAtoms1010Smoke { int x; };\n"
    "DyAtoms1010Smoke *make_atoms_1010_smoke(void) {\n"
    "    static struct DyAtoms1010Smoke a;\n"
    "    a.x = 42;\n"
    "    return &a;\n"
    "}\n"
    "int get_x_1010_smoke(DyAtoms1010Smoke *t) { return t->x; }\n"
)

OPAQUE_HANDLE_1010_USE = (
    '#include "opaque_handle_1010_smoke.h"\n'
    "int main(void) {\n"
    "    DyAtoms1010Smoke *t = make_atoms_1010_smoke();\n"
    "    return get_x_1010_smoke(t);\n"
    "}\n"
)


def _case_opaque_handle_1010_order(cccc: Path, tmp: str, def_first: bool) -> bool:
    write(Path(tmp) / "opaque_handle_1010_smoke.h", OPAQUE_HANDLE_1010_HEADER)
    def_src = Path(tmp) / "opaque_handle_1010_smoke_def.c"
    use_src = Path(tmp) / "opaque_handle_1010_smoke_use.c"
    write(def_src, OPAQUE_HANDLE_1010_DEF)
    write(use_src, OPAQUE_HANDLE_1010_USE)
    order = [def_src.name, use_src.name] if def_first else [use_src.name, def_src.name]

    vm_result = run([str(cccc), *order], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    suffix = "deffirst" if def_first else "usefirst"
    out_bin = Path(tmp) / f"opaque_handle_1010_smoke_out_{suffix}"
    compile_result = run(
        [str(cccc), "-c=native", "-o", out_bin.name, *order], cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    return True


DUP_TAG_1014_HEADER = (
    "#pragma once\n"
    "typedef struct DyGC1014Smoke DyGC1014Smoke;\n"
    "DyGC1014Smoke *gc_open_1014_smoke(void);\n"
    "int gc_val_1014_smoke(DyGC1014Smoke *g);\n"
)

# Header-exposed group: completes the tag with the shape gc_open_1014_smoke/
# gc_val_1014_smoke's own signatures use -- must always keep the plain
# `struct DyGC1014Smoke` spelling.
DUP_TAG_1014_IMPL = (
    '#include "dup_tag_1014_smoke.h"\n'
    "struct DyGC1014Smoke { int v; };\n"
    "static struct DyGC1014Smoke g_1014_smoke = { 42 };\n"
    "DyGC1014Smoke *gc_open_1014_smoke(void) { return &g_1014_smoke; }\n"
    "int gc_val_1014_smoke(DyGC1014Smoke *g) { return g->v; }\n"
)

# Private, differently-shaped completion of the same tag name -- never
# includes the header, so it must always be renamed.
DUP_TAG_1014_PRIVATE = (
    "struct DyGC1014Smoke { double d; char pad; };\n"
    "int priv_use_1014_smoke(void) {\n"
    "    struct DyGC1014Smoke x;\n"
    "    x.d = 1.0;\n"
    "    x.pad = 'a';\n"
    "    return (int)x.d;\n"
    "}\n"
)

DUP_TAG_1014_MAIN = (
    '#include "dup_tag_1014_smoke.h"\n'
    "extern int priv_use_1014_smoke(void);\n"
    "int main(void) {\n"
    "    DyGC1014Smoke *g = gc_open_1014_smoke();\n"
    "    (void)priv_use_1014_smoke();\n"
    "    return gc_val_1014_smoke(g);\n"
    "}\n"
)


def _case_dup_tag_1014_order(cccc: Path, tmp: str, impl_first: bool) -> bool:
    write(Path(tmp) / "dup_tag_1014_smoke.h", DUP_TAG_1014_HEADER)
    impl_src = Path(tmp) / "dup_tag_1014_smoke_impl.c"
    private_src = Path(tmp) / "dup_tag_1014_smoke_private.c"
    main_src = Path(tmp) / "dup_tag_1014_smoke_main.c"
    write(impl_src, DUP_TAG_1014_IMPL)
    write(private_src, DUP_TAG_1014_PRIVATE)
    write(main_src, DUP_TAG_1014_MAIN)
    order = ([impl_src.name, private_src.name] if impl_first
              else [private_src.name, impl_src.name]) + [main_src.name]

    vm_result = run([str(cccc), *order], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    suffix = "implfirst" if impl_first else "privatefirst"
    out_bin = Path(tmp) / f"dup_tag_1014_smoke_out_{suffix}"
    compile_result = run(
        [str(cccc), "-c=native", "-o", out_bin.name, *order], cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    return True


def case_dup_tag_1014_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  75: -c=native, two translation units each independently "
          "completing a same-named but differently-shaped struct tag "
          "(the opaque-handle idiom used per-backend -- one TU is "
          "header-exposed and must keep the plain tag spelling, the other "
          "never includes the header and must be renamed), in both input "
          "orders (#1014). Previously both `struct DyGC1014Smoke { ... };` "
          "bodies serialized under the identical plain tag name -- a host "
          "'redefinition' compile failure no -m shape assertion alone can "
          "see -- this is that proof, VM 42 -> native 42, both orders")
    if not _case_dup_tag_1014_order(cccc, tmp, impl_first=True):
        return False
    if not _case_dup_tag_1014_order(cccc, tmp, impl_first=False):
        return False
    print("    ok")
    return True


# #1015: two translation units each independently declaring a same-named
# enum tag AND a same-named enumerator with a different value/shape.
# #1014's own rename_colliding_type_tags() already renames the colliding
# tag apart (`enum E1015Smoke__cccc_dupN`), but until #1015, the
# enumerator (AA1015Smoke) itself still collided -- a host "redefinition of
# enumerator" compile failure no -m shape assertion alone can see, since it
# only shows up once the host compiler actually builds the output.
DUP_ENUM_1015_A = (
    "enum E1015Smoke { AA1015Smoke = 1, BB1015Smoke = 2 };\n"
    "int a_use_1015_smoke(void) { return AA1015Smoke + BB1015Smoke; }\n"
)

DUP_ENUM_1015_B = (
    "enum E1015Smoke { AA1015Smoke = 5, CC1015Smoke = 6 };\n"
    "int b_use_1015_smoke(void) { return AA1015Smoke + CC1015Smoke; }\n"
)

DUP_ENUM_1015_MAIN = (
    "extern int a_use_1015_smoke(void);\n"
    "extern int b_use_1015_smoke(void);\n"
    "int main(void) {\n"
    "    return a_use_1015_smoke() + b_use_1015_smoke() + 28;\n"
    "}\n"
)


def _case_dup_enum_1015_order(cccc: Path, tmp: str, a_first: bool) -> bool:
    a_src = Path(tmp) / "dup_enum_1015_smoke_a.c"
    b_src = Path(tmp) / "dup_enum_1015_smoke_b.c"
    main_src = Path(tmp) / "dup_enum_1015_smoke_main.c"
    write(a_src, DUP_ENUM_1015_A)
    write(b_src, DUP_ENUM_1015_B)
    write(main_src, DUP_ENUM_1015_MAIN)
    order = ([a_src.name, b_src.name] if a_first
              else [b_src.name, a_src.name]) + [main_src.name]

    vm_result = run([str(cccc), *order], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    suffix = "afirst" if a_first else "bfirst"
    out_bin = Path(tmp) / f"dup_enum_1015_smoke_out_{suffix}"
    compile_result = run(
        [str(cccc), "-c=native", "-o", out_bin.name, *order], cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    return True


def case_dup_enum_1015_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  76: -c=native, two translation units each independently "
          "declaring a same-named enum tag AND a same-named enumerator "
          "with a different value/shape, in both input orders (#1015). "
          "#1014 already renames the colliding tag apart, but the "
          "enumerator itself still collided -- a host 'redefinition of "
          "enumerator' compile failure no -m shape assertion alone can "
          "see -- this is that proof, VM 42 -> native 42, both orders")
    if not _case_dup_enum_1015_order(cccc, tmp, a_first=True):
        return False
    if not _case_dup_enum_1015_order(cccc, tmp, a_first=False):
        return False
    print("    ok")
    return True


# #1016: follow-up to #1014/#1015. Neither rename_colliding_type_tags()
# (#1014, tag vs. tag) nor rename_colliding_enum_constants() (#1015,
# enumerator vs. enumerator) looked at the other's namespace -- C has one
# ordinary identifier namespace at file scope, so an enumerator can collide
# with a plain static/extern/function name too. This exercises all three
# Obj shapes verified reproducing during investigation in one TU: a static
# variable, an external-linkage global, and a function, each colliding with
# a same-named enumerator declared in the other TU.
DUP_ENUM_OBJ_1016_A = (
    "static int AA1016Smoke = 3;\n"
    "int BB1016Smoke = 7;\n"
    "int CC1016Smoke(void) { return 9; }\n"
    "int a_use_1016_smoke(void) { return AA1016Smoke + BB1016Smoke + CC1016Smoke(); }\n"
)

DUP_ENUM_OBJ_1016_B = (
    "enum E1016Smoke { AA1016Smoke = 100, BB1016Smoke = 101, CC1016Smoke = 102 };\n"
    "int b_use_1016_smoke(void) { return AA1016Smoke + BB1016Smoke + CC1016Smoke; }\n"
)

DUP_ENUM_OBJ_1016_MAIN = (
    "extern int a_use_1016_smoke(void);\n"
    "extern int b_use_1016_smoke(void);\n"
    "int main(void) {\n"
    "    return a_use_1016_smoke() - 19 + (b_use_1016_smoke() - 303) + 42;\n"
    "}\n"
)


def _case_dup_enum_obj_1016_order(cccc: Path, tmp: str, a_first: bool) -> bool:
    a_src = Path(tmp) / "dup_enum_obj_1016_smoke_a.c"
    b_src = Path(tmp) / "dup_enum_obj_1016_smoke_b.c"
    main_src = Path(tmp) / "dup_enum_obj_1016_smoke_main.c"
    write(a_src, DUP_ENUM_OBJ_1016_A)
    write(b_src, DUP_ENUM_OBJ_1016_B)
    write(main_src, DUP_ENUM_OBJ_1016_MAIN)
    order = ([a_src.name, b_src.name] if a_first
              else [b_src.name, a_src.name]) + [main_src.name]

    vm_result = run([str(cccc), *order], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    suffix = "afirst" if a_first else "bfirst"
    out_bin = Path(tmp) / f"dup_enum_obj_1016_smoke_out_{suffix}"
    compile_result = run(
        [str(cccc), "-c=native", "-o", out_bin.name, *order], cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    return True


def case_dup_enum_obj_1016_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  77: -c=native, an enum's enumerators each colliding with a "
          "same-named ordinary file-scope identifier (a static variable, "
          "an extern global, and a function) declared in a separate "
          "translation unit, in both input orders (#1016). Neither #1014's "
          "tag rename nor #1015's enumerator-vs-enumerator rename looked at "
          "the ordinary identifier namespace an enumerator also shares -- "
          "a host 'redefinition'/'conflicting types' compile failure no -m "
          "shape assertion alone can see -- this is that proof, "
          "VM 42 -> native 42, both orders")
    if not _case_dup_enum_obj_1016_order(cccc, tmp, a_first=True):
        return False
    if not _case_dup_enum_obj_1016_order(cccc, tmp, a_first=False):
        return False
    print("    ok")
    return True


def case_opaque_handle_multi_tu_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  74: -c=native, the opaque-handle idiom (a header forward-"
          "declares `typedef struct Foo Foo;`, exactly one .c file "
          "supplies `struct Foo { ... };`, another .c file only ever "
          "dereferences the handle) across two command-line input files, "
          "in both orders (#1010). Defect A: with the completing TU parsed "
          "first, the struct's definition was dropped from the output "
          "entirely (a later TU's own header forward declaration -- "
          "record_type_name() prepends -- won find_tag_name()'s first-"
          "match scan and looked header-supplied). Defect B: with the "
          "completing TU parsed last, collect_type()'s same_type_or_origin "
          "dedup let the incomplete Type* claim the definition slot first, "
          "emitting a bare `struct Foo;`. Both are host compile failures "
          "('incomplete definition of type'), which tests/test_serialize_"
          "opaque_handle_1010.c and _rev.c's -m shape assertions catch but "
          "can't prove the resulting binary actually links and runs -- "
          "this is that proof, VM 42 -> native 42, both orders")
    if not _case_opaque_handle_1010_order(cccc, tmp, def_first=True):
        return False
    if not _case_opaque_handle_1010_order(cccc, tmp, def_first=False):
        return False
    print("    ok")
    return True


FLOAT_GLOBAL_INIT_PROGRAM = """
float g = 1.0f;
float h = -2.0f;
float z = 0.0f;

int main(void) {
    if (g != 1.0f) return 1;
    if (h != -2.0f) return 2;
    if (z != 0.0f) return 3;
    return 42;
}
"""


def case_float_global_init_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  78: -c=native, a float global initializer whose value prints "
          "with no decimal point under %.9g (e.g. 1.0f) no longer emits the "
          "invalid token `1f` -- serialize_init_bytes's TY_FLOAT arm now "
          "forces a decimal point before appending the f suffix (#967)")
    return _native_run_case(cccc, tmp, "float_global_init_967", FLOAT_GLOBAL_INIT_PROGRAM)


ANON_MEMBER_ACCESS_PROGRAM = """
struct S {
    struct {
        int i;
    };
    int tag;
};

int main(void) {
    struct S s = { .i = 1, .tag = 2 };
    s.i = 42;
    return s.i == 42 && s.tag == 2 ? 42 : 1;
}
"""


def case_anon_member_access_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  79: -c=native, member access through an anonymous struct/union "
          "member (e.g. s.i where i belongs to an unnamed nested struct) no "
          "longer emits the invalid `s./* unknown */.i` -- ND_MEMBER's else "
          "arm now leaves the anonymous link transparent instead of "
          "printing a placeholder comment (#967)")
    return _native_run_case(cccc, tmp, "anon_member_967", ANON_MEMBER_ACCESS_PROGRAM)


TYPEDEF_ORDER_PROGRAM = """
typedef unsigned char lu_byte;
typedef lu_byte TStatus;

struct GCHeader {
    struct GCHeader *next;
    lu_byte tt;
    lu_byte marked;
};

union Value {
    struct {
        lu_byte tag;
        TStatus st;
    } tagged;
    int plain;
};

int main(void) {
    struct GCHeader h;
    h.next = 0;
    h.tt = 1;
    h.marked = 2;

    union Value v;
    v.tagged.tag = 3;
    v.tagged.st = 4;

    return h.tt + h.marked + v.tagged.tag + v.tagged.st + 32;
}
"""


def case_typedef_order_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  81: -c=native, a struct/union member spelling a scalar typedef "
          "name (e.g. `lu_byte tt;`) no longer serializes ahead of that "
          "typedef's own declaration -- serialize_type_defs_for_owner used "
          "to print every struct/union/enum definition before any typedef "
          "alias unconditionally, two independent passes with no ordering "
          "between them (#1027; tests/test_minilua.c, a real-world corpus, "
          "hit this within its first handful of struct definitions, "
          "'unknown type name lu_byte'). Covers a typedef-of-typedef chain "
          "(TStatus -> lu_byte) and a typedef needed only inside an "
          "anonymous nested struct member, which never gets its own turn "
          "in the top-level struct/union/enum loop at all (no tag, no "
          "alias, nothing to refer back to it by). Asserts VM 42 -> "
          "native 42")
    return _vm_and_native_run_case(cccc, tmp, "typedef_order_1027", TYPEDEF_ORDER_PROGRAM)


UNSIGNED_INT64_LITERAL_PROGRAM = """
double g_double_from_u64 = (double)18446744073709551615ULL;

int main(void) {
    if (g_double_from_u64 != 18446744073709551616.0) return 1;

    volatile unsigned long long umax = 18446744073709551615ULL;
    double d = (double)umax;
    if (d != 18446744073709551616.0) return 2;
    if (d < 0) return 3;

    volatile long long neg = -9223372036854775807LL - 1;
    if (neg >= 0) return 4;
    if ((double)neg != -9223372036854775808.0) return 5;

    return 42;
}
"""


def case_unsigned_int64_literal_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  82: -c=native, a folded ND_NUM integer literal now serializes "
          "with a sign/width-accurate suffix instead of a bare `%lld` of "
          "the raw bit pattern -- an unsigned 64-bit value >= 2^63 (e.g. "
          "18446744073709551615ULL, ULLONG_MAX) used to print as the "
          "unsuffixed text `-1`, which a real host compiler reads back as "
          "a negative `int`; a folded INT64_MIN used to print as the bare "
          "token `-9223372036854775808`, not a valid signed literal at "
          "all ('integer literal is too large...'). Asserts VM 42 -> "
          "native 42 (#1031)")
    return _vm_and_native_run_case(cccc, tmp, "unsigned_int64_literal_1031",
                                    UNSIGNED_INT64_LITERAL_PROGRAM)


VECTOR_SPLAT_AND_SELECT_PROGRAM = """
typedef int v4si __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    v4si vi = {2, 4, 6, 8};
    v4si vi2 = vi / 2;
    if (vi2[0] != 1 || vi2[3] != 4) return 1;

    v4sf vf = {1.0f, 2.0f, 3.0f, 4.0f};
    v4sf vf2 = vf * 2.0f;
    if (vf2[0] != 2.0f || vf2[3] != 8.0f) return 2;

    v4si a = {1, 2, 3, 4};
    v4si b = {10, 20, 30, 40};
    v4si cond = (a < (v4si){3, 3, 3, 3});
    v4si sel = cond ? a : b;
    if (sel[0] != 1 || sel[2] != 30) return 3;

    v4si weird_cond = {1, 0, 5, 0};
    v4si sel2 = weird_cond ? a : b;
    if (sel2[1] != 20 || sel2[2] != 3) return 4;

    int flag = 1;
    v4si whole = flag ? a : b;
    if (whole[0] != 1) return 5;

    return 42;
}
"""


def case_vector_splat_and_select_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  83: -c=native, GNU vector_size expressions (tracker #715) "
          "serialize to portable C instead of a literal AST replay (#1019). "
          "Two gaps: (1) `vector op scalar`'s implicit scalar-broadcast "
          "ND_CAST (usual_arith_conv's internal marker, type.c) used to "
          "print as a real explicit cast (`a / (v4si)5`), which GCC/clang "
          "reject ('invalid conversion between vector type and integer "
          "type of different size') -- they only accept the broadcast "
          "performed implicitly inside the operator. (2) GNU per-lane `?:` "
          "(a vector-typed condition) used to re-emit verbatim as `cond ? "
          "a : b`, a GCC-only extension clang rejects ('used type ... "
          "where arithmetic or pointer type is required') -- now lowered "
          "to portable mask arithmetic. A scalar-condition ternary with "
          "vector arms (standard C, not this extension) still emits as a "
          "plain `?:`. Asserts VM 42 -> native 42")
    return _vm_and_native_run_case(cccc, tmp, "vector_splat_select_1019",
                                    VECTOR_SPLAT_AND_SELECT_PROGRAM)


COMMA_ARG_PROGRAM = """
#define TWO_THEN_INC(x) ((x), (x) + 1)

static int add2(int a, int b) {
    return a + b;
}

int main(void) {
    int r = add2(TWO_THEN_INC(5), 10);
    if (r != 16) return 1;

    int total = 0;
    for (int i = (1, 2, 3), j = 100; i < 6; i++, j += 100) {
        total += i + j;
    }
    if (total != (3 + 100) + (4 + 200) + (5 + 300)) return 2;

    return 42;
}
"""


def case_comma_arg_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  84: -c=native, a comma-expression re-emitted into a "
          "function-call argument position (e.g. a macro like minilua's "
          "`ivalue(r)` expanding to one, #1042(b)) used to split into "
          "extra arguments ('too many arguments to function call') -- "
          "get_precedence(ND_COMMA) is the lowest of any node kind "
          "(serialize.c) but the call sites at a funcall's argument list, "
          "a multi-declarator for-init clause, and a handful of manually-"
          "printed 'X = ...;' initializers outside serialize_expr's own "
          "ND_ASSIGN case all passed parent_prec 0, so the wrapping parens "
          "never fired. Asserts VM 42 -> native 42")
    return _vm_and_native_run_case(cccc, tmp, "comma_arg_1042",
                                    COMMA_ARG_PROGRAM)


DOTTED_LOCAL_PROGRAM = """
[[cccc::comptime]]
Node *doubled(Node *arg) {
    Type *ty_int = __builtin_ast_get_type("int");
    Node *tmp = __builtin_ast_local_var_unique(ty_int);
    Node *two = __builtin_ast_int_literal(2);
    Node *mul = __builtin_ast_binary(NK_MUL, arg, two);
    return __builtin_ast_assign(tmp, mul);
}

int main(void) {
    int r1 = doubled(7);
    if (r1 != 14) return 1;
    int r2 = doubled(20);
    if (r2 != 40) return 2;
    return 42;
}
"""


def case_dotted_local_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  85: -c=native, a local created via "
          "__builtin_ast_local_var_unique (new_unique_name()'s dotted "
          "\".L..N\" scheme, the same one an anonymous global uses) used "
          "to serialize its dotted name verbatim at both its declaration "
          "and every reference (e.g. `int .L..29;`) -- not a legal C "
          "identifier. The local-hoist loop's existing empty-name rename "
          "now covers the dotted case too (#1034). Asserts VM 42 -> "
          "native 42")
    return _vm_and_native_run_case(cccc, tmp, "dotted_local_1034",
                                    DOTTED_LOCAL_PROGRAM)


GLOBAL_BLOCK_SPLICE_PROGRAM = """
[[cccc::comptime]]
Node *emit_counter_helpers(void) {
    return Quote("{ struct Counter { int n; }; void counter_init(struct Counter *c) { c->n = 0; } void counter_bump(struct Counter *c, int by) { c->n += by; } }");
}

emit_counter_helpers();

int main(void) {
    struct Counter c;
    counter_init(&c);
    if (c.n != 0)
        return 1;
    counter_bump(&c, 5);
    counter_bump(&c, 37);
    if (c.n != 42)
        return 2;
    return 42;
}
"""


def case_global_block_splice_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  86: -c=native, a file-scope macro call whose returned "
          "ND_BLOCK is spliced into the token stream for re-parse at "
          "global scope (#233) used to ALSO drain its just-built Objs "
          "into macro_globals -- two copies of every generated function "
          "reaching the merged program, so -c=native printed two "
          "prototypes/bodies per function ('conflicting types'). Fixed by "
          "discarding the drain when the splice path is taken. Separately, "
          "the surviving struct tag (parsed from Quote()'s synthetic "
          "\"<quote>\" pseudo-file) was still wrongly treated as "
          "from_include (record_type_name(), parse_core.c) and its "
          "definition suppressed -- fixed by recognizing exactly the "
          "\"<quote>\" pseudo-file as never from_include, distinct from "
          "tokenize_private_header()'s own \"<...>\" real-header tags "
          "(#1034). Asserts VM 42 -> native 42")
    return _vm_and_native_run_case(cccc, tmp, "global_block_splice_1034",
                                    GLOBAL_BLOCK_SPLICE_PROGRAM)


ANON_AGGREGATE_TYPEDEF_PROGRAM = """
typedef struct { int a[2]; } P, *Pp;
typedef struct { char n[8]; } A;
struct UsesA { A m; };

[[cccc::comptime]]
int check(void) {
    Type *tp = GetType("P");
    Type *tpp = GetType("Pp");
    Type *ta = GetType("A");
    Type *tuses_a = GetType("UsesA");
    if (tp && tpp && ta && tuses_a)
        return 42;
    return 0;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(check())));
}
gen();

int main(void) {
    return result();
}
"""


NATIVE_LM_PROGRAM = (
    "int main(void) { return 42; }\n"
)


def case_native_always_links_lm(cccc: Path, tmp: str) -> bool:
    print("  88: -c=native's native `cc` invocation used to never pass "
          "-lm at all -- invisible on most hosts because glibc >= 2.34 "
          "folds the common math functions into libc.so.6 directly and "
          "macOS's libm is folded into libSystem unconditionally, but a "
          "libm-only symbol (the C23 fmaximum/fminimum/totalorder/etc "
          "family, #774) failed to *link* on Linux/glibc as a result "
          "(#1037/#1051). Fixed by always appending -lm to the native cc "
          "invocation (src/main.c). Asserted here by pointing "
          "CCCC_NATIVE_CC at a logging wrapper script and checking -lm "
          "appears in the recorded argv, rather than depending on a "
          "libm-only symbol that isn't available on every host this suite "
          "runs on")
    src = Path(tmp) / "native_lm_1051.c"
    write(src, NATIVE_LM_PROGRAM)
    out = Path(tmp) / "native_lm_1051_out"
    log = Path(tmp) / "native_lm_1051_argv.log"
    real_cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not real_cc:
        print("    FAIL: no real cc/clang/gcc found to wrap")
        return False
    wrapper = Path(tmp) / "native_lm_1051_cc_wrapper.sh"
    write(wrapper, f"#!/bin/sh\nprintf '%s\\n' \"$@\" >> {log}\nexec {real_cc} \"$@\"\n")
    wrapper.chmod(0o755)
    env = dict(os.environ)
    env["CCCC_NATIVE_CC"] = str(wrapper)
    result = subprocess.run(
        [str(cccc), "-c=native", "-o", out.name, src.name],
        capture_output=True, text=True, cwd=tmp, env=env,
    )
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    if not log.exists() or "-lm" not in log.read_text().splitlines():
        print(f"    FAIL: -lm not found in recorded native cc argv "
              f"({log.read_text() if log.exists() else '<no log>'})")
        return False
    run_result = subprocess.run([f"./{out.name}"], capture_output=True, text=True, cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_anon_aggregate_typedef_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  87: -c=native, a typedef whose right-hand side is an "
          "anonymous struct/union/enum (`typedef struct { ... } P, *Pp;`) "
          "used to serialize `typedef P *Pp;` referring to a `P` that was "
          "never printed at all when P itself is never used by value -- "
          "serialize_typedef_alias() deliberately skips an anonymous "
          "aggregate's own combined `typedef struct {...} P;` line, on the "
          "assumption serialize_struct_def() already printed the body while "
          "walking the usage-collected ctx->defs, which is empty here "
          "('unknown type name P', a host compile failure). "
          "emit_typedef_and_deps() now emits the aggregate body itself when "
          "reached this way, gated by a shared emitted_defs dedup set so a "
          "comptime re-parse's duplicate TypeName record for the same "
          "declaration doesn't print it twice ('typedef redefinition with "
          "different types') -- which in turn needed same_type_or_origin() "
          "to gain a structural TY_ARRAY case, since two independently- "
          "parsed occurrences of an array member (e.g. `char n[8]`) never "
          "shared pointer identity (#1046). Asserts VM 42 -> native 42")
    return _vm_and_native_run_case(cccc, tmp, "anon_aggregate_typedef_1046",
                                    ANON_AGGREGATE_TYPEDEF_PROGRAM)


CONST_PTR_PROGRAM = (
    "int rhandler_a(void) { return 100; }\n"
    "int rhandler_b(void) { return 200; }\n"
    "static int (*const rtable[])(void) = {rhandler_a, rhandler_b};\n"
    "static int read_const(int *const p);\n"
    "static int read_const(int *const p) {\n"
    "    return *p;\n"
    "}\n"
    "int main(void) {\n"
    "    if (rtable[0]() != 100) return 1;\n"
    "    if (rtable[1]() != 200) return 2;\n"
    "    int x = 7;\n"
    "    int *const cp = &x;\n"
    "    void *vp = (void *)cp;\n"
    "    int *back = (int *)vp;\n"
    "    if (*back != 7) return 3;\n"
    "    int y = 9;\n"
    "    if (read_const(&y) != 9) return 4;\n"
    "    return 42;\n"
    "}\n"
)


def case_const_ptr_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  89: a const-qualified *pointer* (`int *const p`, is_const on "
          "the TY_PTR Type itself) used to serialize with its leading "
          "`const` misplaced onto the pointee -- serialize_type() "
          "(src/serialize.c) printed `const ` unconditionally, then fell "
          "through to the TY_PTR case, which recurses into "
          "serialize_type_decl() -- whose TY_PTR branch never emits "
          "pointer-level const at all. Result: `const int (*)(void)` "
          "(pointer to const int) instead of `int (*const)(void)` (const "
          "pointer to int), rejected by the host compiler as an "
          "incompatible function pointer type; same bug latent in a "
          "function parameter's prototype vs. definition. Fixed by "
          "normalizing: a bare (non-typedef'd) pointer no longer prints "
          "pointer-level const here, matching what declarator position "
          "already did. Asserts VM 42 -> native 42 (#1045)")
    return _vm_and_native_run_case(cccc, tmp, "const_ptr_1045",
                                    CONST_PTR_PROGRAM)


COMPTIME_PTR_SHADOW_PROGRAM = (
    "int sentinel = 99;\n"
    "[[cccc::comptime]]\n"
    "int a = 7;\n"
    "[[cccc::comptime]]\n"
    "int b = 35;\n"
    "[[cccc::comptime]]\n"
    "Node *a_ptr(void) { return GetComptimePtr(\"a\"); }\n"
    "[[cccc::comptime]]\n"
    "Node *b_ptr(void) { return GetComptimePtr(\"b\"); }\n"
    "int main(void) {\n"
    "    int *x = a_ptr();\n"
    "    int *y = b_ptr();\n"
    "    if (x == y) return 1;\n"
    "    if (*x != 7) return 2;\n"
    "    if (*y != 35) return 3;\n"
    "    if (sentinel != 99) return 4;\n"
    "    return *x + *y;\n"
    "}\n"
)


def case_comptime_ptr_shadow_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  90: a GetComptimePtr() shadow Obj (make_comptime_shadow_obj, "
          "src/macros.c) used to be linked onto vm->compiler.globals -- a "
          "scratch per-TU list -- by link_comptime_shadow_objs(), running "
          "*after* main.c had already snapshotted merged_prog. The shadow "
          "never reached codegen_func.c's data-segment offset-allocation "
          "loop (which walks `prog`), so its offset stayed 0 and every "
          "GetComptimePtr() result silently aliased data_seg[0] -- a wrong "
          "answer on the plain VM path, not just a -c=native gap. Fixed by "
          "appending each shadow onto `prog`'s own tail at the end of "
          "cc_expand_macros(), which both codegen and the serializer's "
          "rename/definition passes already walk. Asserts VM 42 -> native "
          "42, with a sentinel global and distinct per-shadow values so a "
          "regression to shared-address aliasing can't coincidentally sum "
          "to 42 again (#1049)")
    return _vm_and_native_run_case(cccc, tmp, "comptime_ptr_shadow_1049",
                                    COMPTIME_PTR_SHADOW_PROGRAM)


def case_header_global_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  91: -c=native, a header-sourced global (a `static int x = 40;` "
          "reached only through a plain #include) used to be re-emitted "
          "THREE times -- the replayed #include (which already defines "
          "it), the #918 forward-declare-every-global pass, and "
          "serialize_global_var()'s own definition -- a host "
          "'redefinition' compile failure. Functions already had an "
          "include-provenance gate (function_is_header_supplied(), "
          "src/serialize.c); globals had none. Fixed by adding "
          "global_is_header_supplied(), the global-side mirror, consulted "
          "from both suppression sites. Asserts VM 42 -> native 42 (#1047)")
    hdr = Path(tmp) / "header_global_1047_smoke.h"
    src = Path(tmp) / "header_global_1047_smoke.c"
    write(hdr, "static int header_global_1047_smoke = 40;\n")
    write(src,
          "#include \"header_global_1047_smoke.h\"\n"
          "int main(void) {\n"
          "    if (header_global_1047_smoke != 40) return 1;\n"
          "    return header_global_1047_smoke + 2;\n"
          "}\n")

    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    out_bin = Path(tmp) / "header_global_1047_out"
    compile_result = run(
        [str(cccc), "-c=native", "-o", out_bin.name, src.name], cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


SYNTH_LIBC_INCLUDE_PROGRAM = (
    "[[cccc::comptime]]\n"
    "void generate_wrappers_1050(void) {\n"
    "    Obj *cpy = MakeFunction(\"wrap_memcpy_1050\", MakePointer(GetType(\"void\")));\n"
    "    FunctionAddParam(cpy, \"dst\", MakePointer(GetType(\"void\")));\n"
    "    FunctionAddParam(cpy, \"src\", MakePointer(GetType(\"void\")));\n"
    "    FunctionAddParam(cpy, \"n\", GetType(\"long\"));\n"
    "    WithFn(cpy) {\n"
    "        FunctionSetBody(cpy, MakeReturn(Memcpy(MakeParamRef(cpy, \"dst\"),\n"
    "                                                  MakeParamRef(cpy, \"src\"),\n"
    "                                                  MakeParamRef(cpy, \"n\"))));\n"
    "    }\n"
    "\n"
    "    Obj *cmp = MakeFunction(\"wrap_strcmp_1050\", GetType(\"int\"));\n"
    "    FunctionAddParam(cmp, \"a\", MakePointer(GetType(\"char\")));\n"
    "    FunctionAddParam(cmp, \"b\", MakePointer(GetType(\"char\")));\n"
    "    WithFn(cmp) {\n"
    "        FunctionSetBody(cmp, MakeReturn(Strcmp(MakeParamRef(cmp, \"a\"),\n"
    "                                                  MakeParamRef(cmp, \"b\"))));\n"
    "    }\n"
    "}\n"
    "\n"
    "generate_wrappers_1050();\n"
    "\n"
    "int main(void) {\n"
    "    char src[6] = \"hello\";\n"
    "    char dst[6] = {0};\n"
    "    wrap_memcpy_1050(dst, src, 6);\n"
    "    if (wrap_strcmp_1050(dst, \"hello\") != 0)\n"
    "        return 1;\n"
    "    return 42;\n"
    "}\n"
)


def case_synth_libc_include_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  92: a reflection-API comptime builder (Serialize()'s memcpy, "
          "or the Memcpy()/Strcmp() macros here) can resolve a call to "
          "memcpy/strlen/strcmp/etc with no #include of the declaring "
          "header ever reaching -c=native output -- either a fresh Obj "
          "ensure_libc_fn_decl() (src/reflection.c) synthesizes with no "
          "token/file at all, or a genuine Obj reflection.h's own internal "
          "#include <string.h> parse leaves in scope (compile_macro_"
          "program()'s unconditional implicit_reflection_tokens() call, "
          "not gated on custom-attribute usage) -- never a captured user "
          "#include either way, so auto-capture has nothing to replay. "
          "Both shapes reach 'call to undeclared library function' from "
          "the host compiler. Fixed by register_synth_libc_call() "
          "(reflection.c), reached centrally via var_ref_lookup(), "
          "recording {Obj, header} into vm->compiler.synth_libc_decls; "
          "serialize_synth_libc_includes() (serialize.c) emits the real "
          "header for whichever entries a program's emitted functions "
          "actually call, rather than a prototype that could conflict "
          "with the real declaration. Asserts VM 42 -> native 42 (#1050)")
    return _vm_and_native_run_case(cccc, tmp, "synth_libc_include_1050",
                                    SYNTH_LIBC_INCLUDE_PROGRAM)


def case_comptime_header_not_replayed_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  93: a header reached only via a plain #include, but containing "
          "its own [[cccc::comptime]] declarations (not routed via "
          "'#include @comptime'), used to replay verbatim into -c=native "
          "output -- the host compiler got past the harmlessly-ignored "
          "unknown-attribute warning and then hit the comptime-only body "
          "text (Obj/MakeFunction/GetType, reflection-API constructs with "
          "no host meaning) as 'undeclared identifier'. #896's own marking "
          "only covered directive-level routing (#include @comptime "
          "\"x.h\"); there was no equivalent for the [[cccc::comptime]]/ "
          "__attribute__((comptime)) attribute form itself. Fixed by "
          "marking the containing file cccc-only the moment such a "
          "declaration is recognized (try_extract_attr_macro, "
          "src/preprocess.c) -- excluding tokenize_private_header()'s own "
          "synthetic tags (<implicit-reflection.h>/<building.h>/"
          "<testing.h>) and __builtin_quote's <quote> pseudo-file by exact "
          "match, the same #1034/#892 trap a broader prefix match hit "
          "before. Once suppressed, the header's own typedef and statics "
          "(plan_fn/plan_value/plan_ptr) are re-derived by the existing "
          "from_include compensation machinery instead of relying on the "
          "(now-suppressed) replay. Asserts VM 42 -> native 42 (#1048)")
    hdr = Path(tmp) / "comptime_header_1048_smoke.h"
    src = Path(tmp) / "comptime_header_1048_smoke.c"
    write(hdr,
          "#ifndef COMPTIME_HEADER_1048_SMOKE_H\n"
          "#define COMPTIME_HEADER_1048_SMOKE_H\n"
          "typedef int (*plan_fn_1048)(int);\n"
          "static int plan_value_1048;\n"
          "static plan_fn_1048 plan_ptr_1048;\n"
          "\n"
          "[[cccc::comptime]]\n"
          "static void set_plan_1048(void) {\n"
          "    plan_value_1048 = 14 * 3;\n"
          "}\n"
          "\n"
          "[[cccc::comptime]]\n"
          "void generate_result_1048(void) {\n"
          "    set_plan_1048();\n"
          "    Obj *fn = MakeFunction(\"result_1048\", GetType(\"int\"));\n"
          "    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(plan_value_1048)));\n"
          "}\n"
          "#endif\n")
    write(src,
          "#include \"comptime_header_1048_smoke.h\"\n"
          "generate_result_1048();\n"
          "int main(void) {\n"
          "    return result_1048();\n"
          "}\n")

    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    out_bin = Path(tmp) / "comptime_header_1048_out"
    compile_result = run(
        [str(cccc), "-c=native", "-o", out_bin.name, src.name], cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: -c=native exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


SYNTH_TYPEDEF_INCLUDE_PROGRAM = (
    "[[cccc::comptime]]\n"
    "void generate_typedef_wrappers_1057(void) {\n"
    "    Obj *sz = MakeFunction(\"get_size_1057\", GetType(\"size_t\"));\n"
    "    WithFn(sz) {\n"
    "        FunctionSetBody(sz, MakeReturn(MakeIntLiteral(1057)));\n"
    "    }\n"
    "\n"
    "    Obj *pd = MakeFunction(\"get_ptrdiff_1057\", GetType(\"ptrdiff_t\"));\n"
    "    WithFn(pd) {\n"
    "        FunctionSetBody(pd, MakeReturn(MakeIntLiteral(-7)));\n"
    "    }\n"
    "\n"
    "    Obj *wc = MakeFunction(\"get_wchar_1057\", GetType(\"wchar_t\"));\n"
    "    WithFn(wc) {\n"
    "        FunctionSetBody(wc, MakeReturn(MakeIntLiteral(97)));\n"
    "    }\n"
    "}\n"
    "\n"
    "generate_typedef_wrappers_1057();\n"
    "\n"
    "int main(void) {\n"
    "    if (get_size_1057() != 1057) return 1;\n"
    "    if (get_ptrdiff_1057() != -7) return 2;\n"
    "    if (get_wchar_1057() != 97) return 3;\n"
    "    return 42;\n"
    "}\n"
)


def case_synth_typedef_include_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  94: a comptime builder folding a standard scalar typedef name "
          "-- GetType(\"size_t\")/\"ptrdiff_t\"/\"wchar_t\" -- into a "
          "generated function's signature has no #include reaching "
          "-c=native output for it, split off from #1050 as the type-name "
          "sibling of that ticket's call-resolution fix. "
          "cc_comptime_resolve_type_name() (src/macros.c) demand-splices "
          "the name out of CCCC's own bundled include/stddef.h, so record_"
          "type_name() marks it from_include=true and typedef_alias_header_"
          "suppressed() (src/serialize.c) drops its alias line under the "
          "assumption a user #include supplies it -- but nothing here ever "
          "does. Reaches the host compiler as 'unknown type name'. Fixed by "
          "serialize_synth_typedef_includes(), the type-name sibling of "
          "#1050's serialize_synth_libc_includes(): a small {name, header} "
          "table (size_t/ptrdiff_t/wchar_t -> <stddef.h>, verified to match "
          "the real host's own typedef on every supported combo) plus a "
          "usage walk emitting the real #include on demand, never a "
          "printed typedef. Asserts VM 42 -> native 42 and that -m output "
          "contains '#include <stddef.h>' (#1057)")
    src = Path(tmp) / "synth_typedef_include_1057.c"
    write(src, SYNTH_TYPEDEF_INCLUDE_PROGRAM)

    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "#include <stddef.h>" not in m_result.stdout:
        print(f"    FAIL: -m output missing '#include <stddef.h>'\n"
              f"    {m_result.stdout}")
        return False

    if not _native_run_case(cccc, tmp, "synth_typedef_include_1057",
                             SYNTH_TYPEDEF_INCLUDE_PROGRAM):
        return False

    # Negative case (the plan's own hazard): a program that already
    # declares its own top-level `size_t` must not get a forced, possibly-
    # conflicting <stddef.h> on top of it -- has_colliding_user_typedef()
    # (src/serialize.c) defers to the user's own declaration instead.
    collide_src = Path(tmp) / "synth_typedef_include_1057_collide.c"
    write(collide_src,
          "typedef unsigned long size_t;\n"
          "\n"
          "[[cccc::comptime]]\n"
          "void generate_size_1057_collide(void) {\n"
          "    Obj *fn = MakeFunction(\"get_size_1057_collide\", GetType(\"size_t\"));\n"
          "    WithFn(fn) {\n"
          "        FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));\n"
          "    }\n"
          "}\n"
          "\n"
          "generate_size_1057_collide();\n"
          "\n"
          "int main(void) { return (int)get_size_1057_collide(); }\n")
    collide_m = run([str(cccc), "-m", collide_src.name], cwd=tmp)
    if "#include <stddef.h>" in collide_m.stdout:
        print("    FAIL: -m output forced #include <stddef.h> on top of "
              "the program's own size_t typedef\n"
              f"    {collide_m.stdout}")
        return False
    collide_vm = run([str(cccc), collide_src.name], cwd=tmp)
    if collide_vm.returncode != 42:
        print(f"    FAIL: collide VM exit {collide_vm.returncode}\n"
              f"    {collide_vm.stderr}")
        return False
    return _native_run_case(cccc, tmp, "synth_typedef_include_1057_collide",
                            collide_src.read_text())


SETJMP_PROGRAM = (
    "#include <setjmp.h>\n"
    "\n"
    "struct canary_layout {\n"
    "    jmp_buf env;\n"
    "    unsigned long canary;\n"
    "};\n"
    "\n"
    "static struct canary_layout g;\n"
    "\n"
    "static void unwind(void) {\n"
    "    longjmp(g.env, 42);\n"
    "}\n"
    "\n"
    "int main(void) {\n"
    "    g.canary = 0xC0FFEE1054UL;\n"
    "    int rv = setjmp(g.env);\n"
    "    if (rv == 0) {\n"
    "        unwind();\n"
    "        return 1;\n"
    "    }\n"
    "    if (g.canary != 0xC0FFEE1054UL)\n"
    "        return 2;\n"
    "    return rv;\n"
    "}\n"
)


def case_setjmp_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  95: -c=native, setjmp()/longjmp()/_setjmp/_longjmp used to "
          "print as ordinary calls with the env argument cast to `long *` "
          "-- these builtins' VM-side parameter type (parse_decl.c) -- and "
          "relied on the auto-captured `#include <setjmp.h>` line "
          "resolving to the real host header at native-compile time, which "
          "is fragile (a user -I path that happens to also contain CCCC's "
          "own bundled headers, e.g. this repo's own test harness's own "
          "`-I./include`, shadows the real header with CCCC's declaration-"
          "free copy -- 'call to undeclared library function'). Separately, "
          "CCCC's historical `long long[5]` jmp_buf (40 bytes) is far "
          "smaller than every supported host's real jmp_buf (up to 312 "
          "bytes, glibc aarch64), so the real host setjmp() silently "
          "overran the buffer -- confirmed with a struct canary placed "
          "immediately after jmp_buf, which the pre-fix binary clobbers "
          "deterministically. Fixed by (1) widening jmp_buf to "
          "long long[40] (include/setjmp.h) -- storage stays CCCC's own "
          "structural type, never the host's jmp_buf alias, so guest-"
          "folded sizeof/offsetof still agrees with what native writes; "
          "(2) never replaying the captured `#include <setjmp.h>` line "
          "into native/-m output at all, and instead always lowering all "
          "four builtins to calls to exactly `_setjmp`/`_longjmp` -- plain "
          "`extern`-declared functions on every supported host, unlike "
          "`setjmp` itself, a macro on glibc -- with an explicit "
          "`extern int _setjmp(void *); extern void _longjmp(void *, int) "
          "...;` declaration serialize_synth_setjmp_decls() emits on "
          "demand, and the env arg cast to `(void *)` rather than the "
          "implicit `(long *)`. Asserts VM 42 -> native 42, the canary "
          "survives, and -m output uses '_setjmp'/'_longjmp'/'(void *)' "
          "rather than 'setjmp'/'longjmp'/'(long *)', with no "
          "'#include <setjmp.h>' at all (#1054/#1030)")
    src = Path(tmp) / "setjmp_1054.c"
    write(src, SETJMP_PROGRAM)

    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "(long *)" in m_result.stdout or "#include <setjmp.h>" in m_result.stdout:
        print(f"    FAIL: -m output still casts the env arg to '(long *)' "
              f"or replays '#include <setjmp.h>'\n    {m_result.stdout}")
        return False
    if "(void *)" not in m_result.stdout or "_setjmp" not in m_result.stdout \
            or "_longjmp" not in m_result.stdout:
        print(f"    FAIL: -m output missing the expected '(void *)' cast "
              f"or '_setjmp'/'_longjmp' calls\n    {m_result.stdout}")
        return False

    return _native_run_case(cccc, tmp, "setjmp_1054", SETJMP_PROGRAM)


def main() -> int:
    root = Path(__file__).parent.parent.resolve()
    cccc = root / "cccc"

    print("Native-backend serializer smoke tests (#892/#897/#901/#904/#918/#925/#926/#927/#928/#952/#953/#956/#963/#964/#968/#971/#973/#976/#977/#982/#965/#989/#990/#993/#996/#995/#998/#999/#1002/#1003/#1005/#1006/#1010/#1011/#1014/#1015/#1016/#967/#1031/#1019/#1042/#1034/#1046/#1051/#1045/#1049/#1047/#1050/#1048/#1057/#1054/#1030)")

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
            case_anon_locals_end_to_end,
            case_anon_locals_m_output,
            case_arrays_suite_no_serializer_gaps,
            case_gvar_builders_generated_output,
            case_bare_c_defaults_to_native_a_out,
            case_bare_c_bytecode_defaults_to_a_c4,
            case_emit_cccc_native_requires_explicit_cc,
            case_emit_cccc_native_with_explicit_cc,
            case_emit_cccc_m_output_round_trips,
            case_test_run_clean_program_compiles,
            case_test_run_oob_write_refused,
            case_test_run_basic_level_compiles,
            case_test_run_bytecode_no_global_contamination,
            case_c_generated_defaults_and_aliases,
            case_anon_union_member_not_va_list,
            case_generated_no_duplicate_captured_include,
            case_generated_comptime_include_still_derives,
            case_generated_forward_decls_hoisted,
            case_bitops_native_round_trip,
            case_atomics_native_round_trip,
            case_computed_goto_native_round_trip,
            case_complex_native_round_trip,
            case_convertvector_native_round_trip,
            case_addr_builtins_native_round_trip,
            case_asm_native_round_trip,
            case_complex_nesting_native_round_trip,
            case_vla_native_round_trip,
            case_overflow_native_round_trip,
            case_vla_multidim_native_round_trip,
            case_vla_addr_native_round_trip,
            case_vla_row_sub_and_init_native_round_trip,
            case_vla_partial_init_native_round_trip,
            case_block_capture_native_round_trip,
            case_block_mutable_native_round_trip,
            case_block_nested_copy_native_round_trip,
            case_block_partial_init_native_round_trip,
            case_block_local_type_hoist_native_round_trip,
            case_block_release_no_stdlib_native_round_trip,
            case_block_release_with_stdlib_native_round_trip,
            case_block_header_type_capture_native_round_trip,
            case_block_routed_include_type_capture_native_round_trip,
            case_block_no_literal_preamble_m_output,
            case_block_large_struct_capture_round_trip,
            case_macro_generated_block_locals_round_trip,
            case_macro_generated_block_generated_output_links,
            case_generated_embedded_header_no_duplicate,
            case_generated_embedded_header_comptime_only_still_derives,
            case_native_embedded_header_include_not_suppressed,
            case_dandy_vtable_pattern_multi_tu,
            case_polyfill_header_embedded_round_trip,
            case_static_name_collision_multi_tu,
            case_header_static_fn_mixed_path_spelling_1032,
            case_switch_break_continue_native_round_trip,
            case_multi_tu_typedef_and_includes_native_round_trip,
            case_opaque_handle_multi_tu_native_round_trip,
            case_dup_tag_1014_native_round_trip,
            case_dup_enum_1015_native_round_trip,
            case_dup_enum_obj_1016_native_round_trip,
            case_float_global_init_native_round_trip,
            case_anon_member_access_native_round_trip,
            case_typedef_order_native_round_trip,
            case_unsigned_int64_literal_native_round_trip,
            case_vector_splat_and_select_native_round_trip,
            case_comma_arg_native_round_trip,
            case_dotted_local_native_round_trip,
            case_global_block_splice_native_round_trip,
            case_anon_aggregate_typedef_native_round_trip,
            case_native_always_links_lm,
            case_const_ptr_native_round_trip,
            case_comptime_ptr_shadow_native_round_trip,
            case_header_global_native_round_trip,
            case_synth_libc_include_native_round_trip,
            case_comptime_header_not_replayed_native_round_trip,
            case_synth_typedef_include_native_round_trip,
            case_setjmp_native_round_trip,
        ]
        results = [case(cccc, tmp) for case in cases]

    if all(results):
        print(f"All {len(results)} native-backend serializer smoke cases passed.")
        return 0
    print(f"{results.count(False)} of {len(results)} native-backend serializer smoke cases FAILED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
