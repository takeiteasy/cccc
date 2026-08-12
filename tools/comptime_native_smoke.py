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


def main() -> int:
    root = Path(__file__).parent.parent.resolve()
    cccc = root / "cccc"

    print("Native-backend serializer smoke tests (#892/#897/#901/#904/#918/#925/#926/#927/#928/#952/#953/#956/#963/#964/#968/#971/#973/#976/#977)")

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
        ]
        results = [case(cccc, tmp) for case in cases]

    if all(results):
        print(f"All {len(results)} native-backend serializer smoke cases passed.")
        return 0
    print(f"{results.count(False)} of {len(results)} native-backend serializer smoke cases FAILED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
