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

Pass --audit-skips (#1197) to run the SMOKE_CASE_SKIPS_GCC_MACOS staleness
audit instead of the normal suite: forces every case actually governed by an
entry on this platform+compiler-family to run with its skip bypassed, and
exits 1 iff any of them now passes (a stale entry). See audit_skips() below
and man/TESTING.md's "Skips" section. Unknown args (including this one, when
absent) are otherwise ignored -- this script always targets the repo-root
./cccc.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Ensure tools/ is on sys.path so the 'testing' package is importable both
# standalone and when run_tests.py loads this file via
# importlib.util.spec_from_file_location (which does not add the script's
# own directory to sys.path) -- mirrors tools/testing/test_native_skip_
# audit.py's own explicit insert. run_tests.py's loader wraps the whole
# module exec in `except Exception: return f"FAILED ({e})"`, so a path-
# dependent ImportError here would otherwise surface as an opaque suite
# failure instead of a normal skip.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from testing import smoke_case_skip_reason, smoke_entry_applies_here
from testing.platform import detect_platform, detect_native_cc_family

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


def case_testing_bytecode_prepass_compiles(cccc: Path, tmp: str) -> bool:
    print("  131: --testing -c=bytecode runs the suite as a pre-pass, then writes "
          "the artifact (#1106)")
    src = Path(tmp) / "testing_prepass_bc.c"
    write(src, "[[cccc::test(return = 42)]] int t_pass(void) { return 42; }\n"
               "int main(void) { return 7; }\n")
    out = Path(tmp) / "testing_prepass_bc.c4"
    if out.exists():
        out.unlink()
    result = run([str(cccc), "--testing", "-c=bytecode", "-o", out.name, src.name],
                 cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    if not out.exists():
        print("    FAIL: artifact was not written despite the suite passing\n"
              f"    {result.stderr}")
        return False
    # The saved .c4 must be a real artifact of the guarded program.
    run_result = run([str(cccc), out.name], cwd=tmp)
    if run_result.returncode != 7:
        print(f"    FAIL: reloaded .c4 exited {run_result.returncode}, expected 7\n"
              f"    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_testing_native_prepass_compiles(cccc: Path, tmp: str) -> bool:
    print("  132: --testing -c=native runs the suite as a pre-pass, then builds via "
          "the host toolchain (#1106)")
    src = Path(tmp) / "testing_prepass_native.c"
    write(src, "[[cccc::test(return = 42)]] int t_pass(void) { return 42; }\n"
               "int main(void) { return 42; }\n")
    out = Path(tmp) / "testing_prepass_native_out"
    if out.exists():
        out.unlink()
    result = run([str(cccc), "--testing", "-c=native", "-o", out.name, src.name],
                 cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    if not out.exists():
        print("    FAIL: executable was not written despite the suite passing\n"
              f"    {result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: executable exited {run_result.returncode}, expected 42\n"
              f"    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_testing_failing_suite_refuses_compile(cccc: Path, tmp: str) -> bool:
    print("  133: --testing -c=bytecode refuses to compile (no artifact) when the "
          "suite fails (#1106)")
    src = Path(tmp) / "testing_prepass_fail.c"
    write(src, "[[cccc::test(return = 999)]] int t_fail(void) { return 1; }\n"
               "int main(void) { return 42; }\n")
    out = Path(tmp) / "testing_prepass_fail.c4"
    if out.exists():
        out.unlink()
    result = run([str(cccc), "--testing", "-c=bytecode", "-o", out.name, src.name],
                 cwd=tmp)
    if result.returncode == 0:
        print("    FAIL: compile unexpectedly succeeded for a failing suite")
        return False
    if out.exists():
        print("    FAIL: artifact was written despite failing tests")
        return False
    # The guard is independent of --fail-fast: that flag only stops the test
    # *run* early; a red suite blocks compilation either way.
    result_ff = run(
        [str(cccc), "--testing", "--fail-fast", "-c=bytecode", "-o", out.name,
         src.name], cwd=tmp)
    if result_ff.returncode == 0 or out.exists():
        print("    FAIL: --fail-fast variant should also refuse to compile")
        return False
    print("    ok")
    return True


def case_testing_build_blocked_by_failing_suite(cccc: Path, tmp: str) -> bool:
    print("  134: --testing --build refuses to build when the suite fails, even "
          "without --fail-fast (#1106)")
    src = Path(tmp) / "testing_build_guard.c"
    write(src, "[[cccc::test(return = 999)]] int t_fail(void) { return 1; }\n"
               "[[cccc::build]]\n"
               "int build_main(void) { return 0; }\n")
    result = run([str(cccc), "--testing", "--build", src.name], cwd=tmp)
    if result.returncode == 0:
        print("    FAIL: build proceeded past failing tests without --fail-fast")
        return False
    # Passing suites must still compose with --build (existing behaviour).
    src_ok = Path(tmp) / "testing_build_pass.c"
    write(src_ok, "[[cccc::test(return = 42)]] int t_pass(void) { return 42; }\n"
                  "[[cccc::build]]\n"
                  "int build_main(void) { return 0; }\n")
    result_ok = run([str(cccc), "--testing", "--build", src_ok.name], cwd=tmp)
    if result_ok.returncode != 0:
        print(f"    FAIL: passing-suite build exited {result_ok.returncode}\n"
              f"    {result_ok.stderr}")
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

VA_LIST_SIZE_PROGRAM = (
    "#include <stdarg.h>\n"
    "\n"
    "int main(void) {\n"
    "    if (sizeof(va_list) < 32)\n"
    "        return 1;\n"
    "    if (sizeof(va_list) != 64)\n"
    "        return 2;\n"
    "    return 42;\n"
    "}\n"
)

VA_LIST_TRANSLATION_PROGRAM = (
    "#include <stdarg.h>\n"
    "\n"
    "static double sum_doubles(int count, ...) {\n"
    "    va_list args;\n"
    "    va_start(args, count);\n"
    "    va_list copy;\n"
    "    va_copy(copy, args);\n"
    "    double copy_total = 0.0;\n"
    "    for (int i = 0; i < count; i++)\n"
    "        copy_total += va_arg(copy, double);\n"
    "    va_end(copy);\n"
    "    double total = 0.0;\n"
    "    for (int i = 0; i < count; i++)\n"
    "        total += va_arg(args, double);\n"
    "    va_end(args);\n"
    "    if (copy_total != total)\n"
    "        return -1.0;\n"
    "    return total;\n"
    "}\n"
    "\n"
    "int main(void) {\n"
    "    double d = sum_doubles(4, 1.0, 2.0, 3.0, 4.0);\n"
    "    if (d != 10.0)\n"
    "        return 1;\n"
    "    return 42;\n"
    "}\n"
)

VA_ARG_PROMOTION_PROGRAM = (
    "#include <stdarg.h>\n"
    "\n"
    "static float sum_floats(int n, ...) {\n"
    "    va_list ap;\n"
    "    va_start(ap, n);\n"
    "    float total = 0.0f;\n"
    "    for (int i = 0; i < n; i++)\n"
    "        total += va_arg(ap, float);\n"
    "    va_end(ap);\n"
    "    return total;\n"
    "}\n"
    "\n"
    "static int sum_chars(int n, ...) {\n"
    "    va_list ap;\n"
    "    va_start(ap, n);\n"
    "    int total = 0;\n"
    "    for (int i = 0; i < n; i++)\n"
    "        total += va_arg(ap, char);\n"
    "    va_end(ap);\n"
    "    return total;\n"
    "}\n"
    "\n"
    "int main(void) {\n"
    "    if (sum_floats(2, 1.5f, 2.5f) != 4.0f)\n"
    "        return 1;\n"
    "    if (sum_chars(2, (char)20, (char)22) != 42)\n"
    "        return 2;\n"
    "    return 42;\n"
    "}\n"
)

STDARG_GUARD_PROGRAM = (
    "#include <stdio.h>\n"
    "#include <stdarg.h>\n"
    "\n"
    "static int sum_ints(int n, ...) {\n"
    "    va_list ap;\n"
    "    va_start(ap, n);\n"
    "    int total = 0;\n"
    "    for (int i = 0; i < n; i++)\n"
    "        total += va_arg(ap, int);\n"
    "    va_end(ap);\n"
    "    return total;\n"
    "}\n"
    "\n"
    "int main(void) {\n"
    "    FILE *f = 0;\n"
    "    (void)f;\n"
    "    if (sum_ints(3, 10, 12, 20) != 42)\n"
    "        return 1;\n"
    "    return 42;\n"
    "}\n"
)

ISSIGNALING_PROGRAM = (
    "#include <math.h>\n"
    "\n"
    "int main(void) {\n"
    "    float sf = 0.0f / 0.0f;\n"
    "    double sd = 0.0 / 0.0;\n"
    "    int a = issignaling(sf);\n"
    "    int b = issignaling(sd);\n"
    "    int c = iseqsig(1.0f, 1.0f);\n"
    "    int d = iseqsig(1.0, 1.0);\n"
    "    if (a != 0 && a != 1) return 1;\n"
    "    if (b != 0 && b != 1) return 2;\n"
    "    if (!c) return 3;\n"
    "    if (!d) return 4;\n"
    "    return 42;\n"
    "}\n"
)

DOUBLE_LITERAL_PROGRAM = (
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "\n"
    "int main(void) {\n"
    "    char buf[64];\n"
    "    snprintf(buf, sizeof buf, \"%g\", 55.0);\n"
    "    if (strcmp(buf, \"55\") != 0)\n"
    "        return 1;\n"
    "    snprintf(buf, sizeof buf, \"%g %g %g\", 0.0, 100.0, -58.0);\n"
    "    if (strcmp(buf, \"0 100 -58\") != 0)\n"
    "        return 2;\n"
    "    return 42;\n"
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


def case_double_literal_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  96: -c=native, serialize_expr's ND_NUM TY_DOUBLE arm printed a "
          "bare `%.17g` of the folded value with no \".0\"-if-integral "
          "fixup -- unlike the TY_FLOAT/TY_LDOUBLE arms right next to it, "
          "which already apply exactly that fixup (#1038). An integral "
          "double like 55.0 serialized as the bare text \"55\", read back "
          "by a real host compiler as an *integer* literal. Harmless under "
          "an enclosing (double) cast, a real wrong answer at a variadic "
          "call site, where the argument expression's own printed type "
          "(not its C-level static type) decides which register/slot the "
          "host compiler places it in -- found while root-causing #1018 "
          "(tests/repro_varargs.c). Fixed by routing the TY_DOUBLE arm "
          "through the same fixup format_float_literal/"
          "format_ldouble_literal already use (#1058). Asserts -m output "
          "prints '55.0' (not bare '55') for a folded integral double "
          "literal, and VM 42 -> native 42 for a program that would "
          "silently misdirect a variadic host-libc call otherwise.")
    src = Path(tmp) / "double_literal_1058.c"
    write(src, DOUBLE_LITERAL_PROGRAM)

    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "55.0" not in m_result.stdout:
        print(f"    FAIL: -m output doesn't print the integral double "
              f"literal as '55.0'\n    {m_result.stdout}")
        return False

    return _native_run_case(cccc, tmp, "double_literal_1058", DOUBLE_LITERAL_PROGRAM)


def case_va_list_size_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  97: -c=native, guest-folded sizeof(va_list) used to fold "
          "against CCCC's own 24-byte struct va_list layout (include/"
          "stdarg.h), while the replayed `#include <stdarg.h>` resolves to "
          "the real host's own, larger va_list at native-compile time -- "
          "same soundness class #1054 documented for jmp_buf/setjmp.h. "
          "Measured the real host va_list size directly on every "
          "supported platform x arch combo: macOS arm64 8 bytes, macOS "
          "x86_64 24, glibc x86_64 32, glibc aarch64 32. Fixed by padding "
          "CCCC's struct va_list to 64 bytes (a trailing char __reserved"
          "[40]) so the folded constant over-allocates on every one of "
          "them, mirroring #1054's jmp_buf widening (#1059). Asserts -m "
          "output folds sizeof(va_list) to exactly 64, and VM 42 -> "
          "native 42.")
    src = Path(tmp) / "va_list_size_1059.c"
    write(src, VA_LIST_SIZE_PROGRAM)

    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "64ULL" not in m_result.stdout and "64" not in m_result.stdout:
        print(f"    FAIL: -m output doesn't fold sizeof(va_list) to "
              f"64\n    {m_result.stdout}")
        return False

    return _native_run_case(cccc, tmp, "va_list_size_1059", VA_LIST_SIZE_PROGRAM)


def case_va_list_translation_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  98: -c=native, <stdarg.h>'s va_start/va_arg/va_copy/va_end "
          "macros used to expand directly into VM-ABI pointer arithmetic "
          "over CCCC's own struct va_list (reg_ptr/stack_ptr/reg_count) "
          "and __builtin_frame_address(0) -- the serializer printed that "
          "expansion verbatim, which a real host compiler rejects outright "
          "('member reference base type va_list (aka char *) is not a "
          "structure or union') since the replayed #include <stdarg.h> "
          "resolves to the real, differently-shaped host va_list at "
          "native-compile time (#1018). Fixed by wrapping each macro's "
          "existing VM-ABI expansion (unchanged) as the trailing argument "
          "to a new internal __cccc_va_start/_arg/_copy/_end builtin "
          "(src/parse_postfix.c) that parses ap/last/type/src a second, "
          "independent time purely to stamp them as serializer annotation "
          "(Node.va_form, src/cccc.h) on the returned, otherwise-identical "
          "impl node -- VM codegen/comptime/reflection/inlining see "
          "byte-identical AST throughout; only serialize_expr prints the "
          "real host form instead of walking the VM-internal subtree. "
          "Asserts -m output contains 'va_start('/'va_arg('/'va_copy('/"
          "'va_end(' and none of 'reg_ptr'/'reg_count'/'stack_ptr', and "
          "VM 42 -> native 42 including a va_copy independent-walk check.")
    src = Path(tmp) / "va_list_translation_1018.c"
    write(src, VA_LIST_TRANSLATION_PROGRAM)

    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    for needed in ("va_start(", "va_arg(", "va_copy(", "va_end("):
        if needed not in m_result.stdout:
            print(f"    FAIL: -m output missing '{needed}'\n    {m_result.stdout}")
            return False
    for leaked in ("reg_ptr", "reg_count", "stack_ptr"):
        if leaked in m_result.stdout:
            print(f"    FAIL: -m output still leaks VM-internal '{leaked}'\n"
                  f"    {m_result.stdout}")
            return False

    return _native_run_case(cccc, tmp, "va_list_translation_1018", VA_LIST_TRANSLATION_PROGRAM)


def case_va_arg_promotion_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  99: -c=native, va_arg(ap, T) printed T verbatim from the "
          "user's own macro argument text -- but C's default argument "
          "promotions (C17 6.5.2.2p6/7) mean a real variadic call site "
          "never actually places a bare float/char/short/bool in a "
          "variadic slot, so a real host compiler's own <stdarg.h> "
          "diagnoses an unpromoted promotable T as undefined behavior "
          "('second argument to va_arg is of promotable type float/char/"
          "...'). CCCC's own VM-ABI read always reads a full 8-byte slot "
          "regardless of the requested width, so this had no VM-visible "
          "symptom -- only a native compiler diagnostic, found during a "
          "post-#1018 self-check. Fixed by va_arg_promoted_type() (src/"
          "serialize.c): the VA_ARG print site now maps TY_FLOAT -> double "
          "and any integer type narrower than int -> int before printing, "
          "mirroring type.c's own integer_promotion() for the integer "
          "half. Asserts -m output prints 'va_arg(ap, double)'/'va_arg(ap, "
          "int)', never the bare 'float'/'char' spelling, and VM 42 -> "
          "native 42.")
    src = Path(tmp) / "va_arg_promotion_1018.c"
    write(src, VA_ARG_PROMOTION_PROGRAM)

    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "va_arg(ap, float)" in m_result.stdout or "va_arg(ap, char)" in m_result.stdout:
        print(f"    FAIL: -m output still prints an unpromoted va_arg "
              f"type\n    {m_result.stdout}")
        return False
    if "va_arg(ap, double)" not in m_result.stdout or "va_arg(ap, int)" not in m_result.stdout:
        print(f"    FAIL: -m output missing the expected promoted va_arg "
              f"forms\n    {m_result.stdout}")
        return False

    return _native_run_case(cccc, tmp, "va_arg_promotion_1018", VA_ARG_PROMOTION_PROGRAM)


def case_stdarg_guard_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  100: -c=native, include/stdarg.h's own outer include guard "
          "used to wrap the ENTIRE file, including the '#else "
          "#include_next <stdarg.h> #endif' hand-off to the real host "
          "header -- found on real Linux (clang 18) while verifying "
          "#1018's translation there, every prior pass in this batch "
          "having been macOS-only. glibc's real <stdio.h> issues its own "
          "PARTIAL stdarg.h request first ('#define __need___va_list' "
          "then '#include <stdarg.h>', wanting only __gnuc_va_list) to "
          "pick up its own prototypes; clang's real <stdarg.h> handles a "
          "partial request correctly (it doesn't set its own guard, so a "
          "later full request still runs) but CCCC's own outer guard was "
          "already permanently set by that first partial pass, so a "
          "LATER, full '#include <stdarg.h>' skipped the #include_next "
          "hand-off entirely -- va_start/va_arg/va_end never got "
          "macro-defined at all ('call to undeclared library function "
          "va_start'). Fixed by moving the guard to wrap only the "
          "'#ifdef __CCCC__' branch's own body, leaving the '#else' "
          "branch's #include_next unconditional. Asserts VM 42 -> native "
          "42 for <stdio.h> included before <stdarg.h>.")
    src = Path(tmp) / "stdarg_guard_1018.c"
    write(src, STDARG_GUARD_PROGRAM)

    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    return _native_run_case(cccc, tmp, "stdarg_guard_1018", STDARG_GUARD_PROGRAM)


def case_issignaling_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  101: -c=native, include/math.h's own issignaling_f/issignaling_d/"
          "iseqsig_f/iseqsig_d externs (:535-541) used to stay unguarded "
          "while the matching isnan_f/isinf_f/signbit_f/fpclassify_f block "
          "right above them (:56-67) was already guarded on __CCCC__ since "
          "#1021 -- #1052 added these four native_accessor_shims entries "
          "(src/serialize.c) but missed guarding their declarations here, "
          "found only on a real Linux run since the collision needs "
          "CCCC's own header (via -I./include) to win the search over the "
          "real host <math.h>. A real host compiler rejects the resulting "
          "static-after-non-static redeclaration outright. Fixed by "
          "wrapping the four declarations in #ifdef __CCCC__, matching "
          "the block above them. Unlike every other case in this file, "
          "this one deliberately passes an explicit -I<repo>/include "
          "itself (not _native_run_case's plain invocation) -- without "
          "it, the replayed #include <math.h> would resolve to the real "
          "host header, which declares none of these names, and the case "
          "would pass vacuously whether or not the guard is present.")
    src = Path(tmp) / "issignaling_1063.c"
    write(src, ISSIGNALING_PROGRAM)
    include_dir = cccc.parent / "include"

    vm_result = run([str(cccc), "-I", str(include_dir), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    out = Path(tmp) / "issignaling_1063_out"
    compile_result = run(
        [str(cccc), "-I", str(include_dir), "-c=native", "-o", out.name, src.name],
        cwd=tmp,
    )
    if compile_result.returncode != 0:
        print(f"    FAIL: native compile exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False

    print("    ok")
    return True


def case_native_std_ladder(cccc: Path, tmp: str) -> bool:
    print("  102: -c=native used to only forward -std= to the host cc when "
          "the user passed --std= explicitly on the CCCC command line -- a "
          "plain 'cccc foo.c -c=native' relied entirely on the host cc's "
          "own default standard, which can be older than CCCC's own "
          "resolved default (gnu23) and silently reject a legitimately-"
          "emitted C23 construct (#1053). Fixed by probing the host cc "
          "(src/main.c's native_resolve_std_ladder()) down a ladder from "
          "the resolved default toward older standards and forwarding the "
          "newest rung actually accepted. Asserted here by pointing "
          "CCCC_NATIVE_CC at a logging wrapper script and checking a "
          "'-std=gnu' flag appears in the recorded argv even with no "
          "--std on the CCCC command line -- not a specific year, since "
          "the accepted rung is host-dependent by design.")
    src = Path(tmp) / "native_std_ladder_1053.c"
    write(src, NATIVE_LM_PROGRAM)
    out = Path(tmp) / "native_std_ladder_1053_out"
    log = Path(tmp) / "native_std_ladder_1053_argv.log"
    real_cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not real_cc:
        print("    FAIL: no real cc/clang/gcc found to wrap")
        return False
    wrapper = Path(tmp) / "native_std_ladder_1053_cc_wrapper.sh"
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
    lines = log.read_text().splitlines() if log.exists() else []
    if not any(l.startswith("-std=gnu") for l in lines):
        print(f"    FAIL: no '-std=gnu...' flag found in recorded native cc "
              f"argv ({lines})")
        return False
    run_result = subprocess.run([f"./{out.name}"], capture_output=True, text=True, cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_native_explicit_std_probed(cccc: Path, tmp: str) -> bool:
    print("  107: an explicit --std= used to be forwarded to the host cc "
          "verbatim, bypassing #1053's own ladder probe entirely -- hitting "
          "the exact spelling asymmetry the ladder exists to route around "
          "(real GCC 13 rejects '-std=c23'/'-std=gnu23' outright but "
          "accepts '-std=c2x'/'-std=gnu2x'; measured directly in the "
          "cccc-linux-arm64 container, not recalled) (#1073). Fixed by "
          "routing an explicit --std= through the same probe, restricted "
          "to spellings of the SAME standard only (never descending to an "
          "older one -- a user who named C23 must never silently get C17 "
          "semantics on the native half). Asserted here via a wrapper that "
          "rejects '-std=c23' specifically (simulating GCC's asymmetry on "
          "any host, including clang-only ones) and exec's the real cc for "
          "every other flag: the recorded argv must contain '-std=c2x' "
          "exactly, not merely some '-std=' flag -- a presence-only check "
          "would pass even against the pre-fix binary, which also forwards "
          "some '-std=' (just the rejected spelling).")
    src = Path(tmp) / "native_explicit_std_1073.c"
    write(src, NATIVE_LM_PROGRAM)
    out = Path(tmp) / "native_explicit_std_1073_out"
    log = Path(tmp) / "native_explicit_std_1073_argv.log"
    real_cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not real_cc:
        print("    FAIL: no real cc/clang/gcc found to wrap")
        return False
    wrapper = Path(tmp) / "native_explicit_std_1073_cc_wrapper.sh"
    write(wrapper, (
        "#!/bin/sh\n"
        f"printf '%s\\n' \"$@\" >> {log}\n"
        "for a in \"$@\"; do\n"
        "  if [ \"$a\" = \"-std=c23\" ]; then exit 1; fi\n"
        "done\n"
        f"exec {real_cc} \"$@\"\n"
    ))
    wrapper.chmod(0o755)
    env = dict(os.environ)
    env["CCCC_NATIVE_CC"] = str(wrapper)
    result = subprocess.run(
        [str(cccc), "--std=c23", "-c=native", "-o", out.name, src.name],
        capture_output=True, text=True, cwd=tmp, env=env,
    )
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    lines = log.read_text().splitlines() if log.exists() else []
    if "-std=c2x" not in lines:
        print(f"    FAIL: expected '-std=c2x' in recorded native cc argv "
              f"(the rung the ladder falls back to when '-std=c23' is "
              f"rejected), got {lines}")
        return False
    run_result = subprocess.run([f"./{out.name}"], capture_output=True, text=True, cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: exit {run_result.returncode}\n    {run_result.stderr}")
        return False
    print("    ok")
    return True


def case_native_defines_survive_argv(cccc: Path, tmp: str) -> bool:
    print("  103: two -D flags used to reach the host cc mangled two "
          "different ways (#1065): parse_define() (src/main.c) split each "
          "defines[] entry in place at its '=' the first time it ran "
          "(before -c=native's own argv assembly ever sees the array "
          "again), permanently truncating '-DA=1' down to 'A' -- and "
          "separately, run_native_backend() pushed each -D/-U/-l/-std "
          "flag's address from a reused stack buffer straight into the "
          "argv, rather than a copy. Fixed by having parse_define() split "
          "via a bounded copy instead of mutating in place, and by giving "
          "run_native_backend() its own heap-backed StringArray (mirroring "
          "src/build.c's push_compile_flags()). Asserted here via the "
          "logging-wrapper pattern: both '-DA=1' and '-DB=2' must appear "
          "verbatim (not truncated, not duplicated) in the recorded argv.")
    src = Path(tmp) / "native_defines_1065.c"
    write(src, NATIVE_LM_PROGRAM)
    out = Path(tmp) / "native_defines_1065_out"
    log = Path(tmp) / "native_defines_1065_argv.log"
    real_cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not real_cc:
        print("    FAIL: no real cc/clang/gcc found to wrap")
        return False
    wrapper = Path(tmp) / "native_defines_1065_cc_wrapper.sh"
    write(wrapper, f"#!/bin/sh\nprintf '%s\\n' \"$@\" >> {log}\nexec {real_cc} \"$@\"\n")
    wrapper.chmod(0o755)
    env = dict(os.environ)
    env["CCCC_NATIVE_CC"] = str(wrapper)
    result = subprocess.run(
        [str(cccc), "-c=native", "-D", "A=1", "-D", "B=2", "-o", out.name, src.name],
        capture_output=True, text=True, cwd=tmp, env=env,
    )
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    lines = log.read_text().splitlines() if log.exists() else []
    if "-DA=1" not in lines or "-DB=2" not in lines:
        print(f"    FAIL: expected '-DA=1' and '-DB=2' both in recorded "
              f"native cc argv, got {lines}")
        return False
    print("    ok")
    return True


def case_native_signed_char_argv(cccc: Path, tmp: str) -> bool:
    print("  104: -c=native never told the host cc that plain 'char' should "
          "be signed -- CCCC's own ty_char (src/type.c) is signed on every "
          "platform, but a real host's plain 'char' isn't universally so "
          "(glibc/aarch64 defines __CHAR_UNSIGNED__; measured directly in "
          "the cccc-linux-arm64 container). A GNU vector_size lane read as "
          "*((char *)&v + i) and compared against a signed constant (-1, "
          "-11, ...) silently gave the wrong answer there with no compile "
          "error -- reproduced by hand-compiling -m output with "
          "-funsigned-char and matching #1064's exact reported exits (19, "
          "20) (#1064). Fixed by unconditionally forwarding -fsigned-char "
          "to the host cc, unprobed (unlike #1053's -std ladder) since the "
          "flag has existed in both gcc and clang for decades on every "
          "target. Asserted here via the logging-wrapper pattern: "
          "'-fsigned-char' must appear in the recorded argv.")
    src = Path(tmp) / "native_signed_char_1064.c"
    write(src, NATIVE_LM_PROGRAM)
    out = Path(tmp) / "native_signed_char_1064_out"
    log = Path(tmp) / "native_signed_char_1064_argv.log"
    real_cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not real_cc:
        print("    FAIL: no real cc/clang/gcc found to wrap")
        return False
    wrapper = Path(tmp) / "native_signed_char_1064_cc_wrapper.sh"
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
    lines = log.read_text().splitlines() if log.exists() else []
    if "-fsigned-char" not in lines:
        print(f"    FAIL: expected '-fsigned-char' in recorded native cc "
              f"argv, got {lines}")
        return False
    print("    ok")
    return True


COND_DIRECTIVE_PROGRAM = (
    "#ifdef __CCCC__\n"
    "#define TOOK_TAKEN_BRANCH 1\n"
    "#endif\n"
    "#if 1\n"
    "#define ALSO_TAKEN 1\n"
    "#endif\n"
    "#ifndef TOOK_TAKEN_BRANCH\n"
    "#error \"taken-branch #define was lost\"\n"
    "#endif\n"
    "#ifndef ALSO_TAKEN\n"
    "#error \"taken #if 1 branch was lost\"\n"
    "#endif\n"
    "int main(void) {\n"
    "    return 42;\n"
    "}\n"
)


def case_native_cond_directive_not_replayed(cccc: Path, tmp: str) -> bool:
    print("  105: a captured conditional-group directive line "
          "(#if/#ifdef/.../#endif) used to replay verbatim into -m/"
          "-c=native/-c=generated output as an always-empty shell -- CCCC's "
          "own preprocessor had already resolved the guarded content, so "
          "the shell carried no information but still handed a second, "
          "independent evaluation to the host compiler. Two real hazards: "
          "a host lacking a feature-test macro CCCC's own preprocessor "
          "already resolved (clang 18 rejecting a captured "
          "'#if __has_embed(...)' shell outright, test_has_embed.c), and a "
          "captured '#ifdef __CCCC__' shell being silently false at the "
          "host (which never defines that macro), dropping whatever a "
          "taken branch captured (#1064). Fixed by dropping conditional-"
          "group directive lines from cc_serialize_program()'s "
          "emit_directives replay loop (src/serialize.c), gated off "
          "under --emit-cccc like the two existing filters in that loop. "
          "Asserts -m output contains no '#if'/'#ifdef'/'#endif' line, and "
          "VM 42 -> native 42 for a program whose taken branches define "
          "macros the guarded #ifndef/#error checks below them depend on.")
    src = Path(tmp) / "cond_directive_1064.c"
    write(src, COND_DIRECTIVE_PROGRAM)
    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    for line in m_result.stdout.splitlines():
        stripped = line.strip()
        if stripped.startswith("#if") or stripped.startswith("#ifdef") or \
           stripped.startswith("#ifndef") or stripped.startswith("#endif") or \
           stripped.startswith("#else") or stripped.startswith("#elif"):
            print(f"    FAIL: -m output still replays a conditional "
                  f"directive line: {line!r}")
            return False
    return _vm_and_native_run_case(cccc, tmp, "cond_directive_1064",
                                    COND_DIRECTIVE_PROGRAM)


FLT_ROUNDS_PROGRAM = (
    "#include <fenv.h>\n"
    "#include <float.h>\n"
    "int main(void) {\n"
    "    if (fesetround(FE_TOWARDZERO) != 0) return 1;\n"
    "    if (FLT_ROUNDS != 0) return 2;\n"
    "    if (fesetround(FE_TONEAREST) != 0) return 3;\n"
    "    if (FLT_ROUNDS != 1) return 4;\n"
    "    return 42;\n"
    "}\n"
)


def case_flt_rounds_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  106: -c=native's __cccc_flt_rounds shim (native_accessor_shims, "
          "src/serialize.c) used to call __builtin_flt_rounds() -- clang "
          "implements this builtin, but GCC 13 does not, so a program using "
          "FLT_ROUNDS failed to link natively on real GCC with an undefined "
          "reference (#1071; found via tests/test_fenv.c in the "
          "cccc-linux-arm64 container). On clang the pre-fix shim works "
          "fine, so a plain VM 42 -> native 42 round-trip alone would pass "
          "vacuously here -- also asserts the -m output text directly: the "
          "shim must call fegetround() (the same mapping src/stdlib/fenv.c's "
          "own VM-side __cccc_flt_rounds() already uses) and must not call "
          "__builtin_flt_rounds at all. Deliberately uses only <fenv.h>/"
          "<float.h>, not <math.h>, so this case is unaffected by #1070's "
          "still-open, unrelated <stdint.h> #include_next gap.")
    src = Path(tmp) / "flt_rounds_1071.c"
    write(src, FLT_ROUNDS_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "__builtin_flt_rounds" in m_result.stdout:
        print("    FAIL: -m output still calls __builtin_flt_rounds "
              "(clang-only, GCC 13 lacks it)")
        return False
    if "fegetround" not in m_result.stdout:
        print("    FAIL: -m output's __cccc_flt_rounds shim doesn't call "
              f"fegetround()\n    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "flt_rounds_1071",
                                    FLT_ROUNDS_PROGRAM)


MB_CUR_MAX_PROGRAM = (
    "#include <limits.h>\n"
    "#include <locale.h>\n"
    "#include <stdlib.h>\n"
    "\n"
    "int main(void) {\n"
    "    setlocale(LC_ALL, \"C\");\n"
    "    if (MB_CUR_MAX != 1) return 1;\n"
    "    if (MB_CUR_MAX < 1 || MB_CUR_MAX > MB_LEN_MAX) return 2;\n"
    "    return 42;\n"
    "}\n"
)


def case_mb_cur_max_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  109: -c=native, include/stdlib.h had no MB_CUR_MAX at all "
          "(C17 7.22.1) -- unlike MB_LEN_MAX (limits.h, #1067, a "
          "compile-time upper bound), MB_CUR_MAX is genuinely runtime/"
          "locale-dependent (glibc: a function call, __ctype_get_mb_cur_"
          "max(); macOS: a plain global, __mb_cur_max), so it needed an "
          "accessor shim like errno/stdout/FLT_ROUNDS, not a plain macro "
          "constant. CCCC never calls setlocale() itself and the guest's "
          "own setlocale()/mblen()/mbtowc()/etc are all real host FFI "
          "passthroughs, so the host process's locale already is the "
          "guest's -- no separate locale model needed (#1069). Fixed by "
          "__cccc_mb_cur_max (native_accessor_shims, src/serialize.c; "
          "wrap_mb_cur_max, src/stdlib/stdlib.c for the VM side). Unlike "
          "the errno/stdout/FLT_ROUNDS shims, this one does NOT resolve "
          "the infinite-recursion trap by re-#include-ing <stdlib.h> a "
          "second time -- a first attempt at giving stdlib.h its own "
          "#include_next hand-off (the stdio.h/errno.h/fenv.h/math.h "
          "pattern) chased the real host's own header chain deep enough "
          "to hit a second, unrelated instance of #1054's own "
          "-I./include shadowing hazard (a bundled sys/time.h -> "
          "time.h -> clock_t collision with the real host's sys/types.h, "
          "found via the real glibc container) -- no clean stopping "
          "point, so the shim instead spells the host's own internal "
          "accessor directly (verified against the real headers on both "
          "hosts) and never touches <stdlib.h> a second time. Deliberately "
          "passes an explicit -I<repo>/include itself, like case 101, so "
          "both CCCC's own bundled stdlib.h AND (implicitly, since the "
          "shim no longer depends on which one wins) the host's own get "
          "exercised.")
    src = Path(tmp) / "mb_cur_max_1069.c"
    write(src, MB_CUR_MAX_PROGRAM)
    include_dir = cccc.parent / "include"

    vm_result = run([str(cccc), "-I", str(include_dir), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    out = Path(tmp) / "mb_cur_max_1069_out"
    compile_result = run(
        [str(cccc), "-I", str(include_dir), "-c=native", "-o", out.name, src.name],
        cwd=tmp,
    )
    if compile_result.returncode != 0:
        print(f"    FAIL: native compile exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False

    print("    ok")
    return True


NESTED_DECL_BINDING_PROGRAM = (
    "int helper(int x) { return x + 1; }\n"
    "int main(void) {\n"
    "    int helper(int); // redundant block-scope prototype\n"
    "    return helper(41);\n"
    "}\n"
)


def case_nested_decl_binding_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  108: a bodyless block-scope function declaration used to "
          "retroactively flip an already-defined, already-codegen'd "
          "file-scope function to is_nested/is_static -- src/parse_decl.c's "
          "\"Set up nested function tracking\" block ran on whatever Obj "
          "find_func() returned before checking whether a body follows, and "
          "is_nested is just current_fn != NULL. A block-scope declaration "
          "with no storage-class specifier has external linkage (C17 "
          "6.2.2p5): it names the outer helper, it isn't a nested one. "
          "Wrong on the VM (the call site starts passing a static link the "
          "callee's body was never compiled to expect -- args shift by one, "
          "a silent wrong answer, #1056) and on -c=native (the spurious "
          "is_static made the serializer print `static int helper(int)` for "
          "a function with external linkage). Asserts the -m output has no "
          "`static` on helper's definition, in addition to the VM 42 -> "
          "native 42 round-trip.")
    src = Path(tmp) / "nested_decl_binding_1056.c"
    write(src, NESTED_DECL_BINDING_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "static int helper(int x)" in m_result.stdout:
        print("    FAIL: -m output still marks helper() static -- the "
              "block-scope prototype retroactively flipped it\n"
              f"    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "nested_decl_binding_1056",
                                    NESTED_DECL_BINDING_PROGRAM)


NESTED_FN_PROGRAM = (
    "static int outer_no_upvar(int base) {\n"
    "    int inner_no_upvar(int x) { return x * 2; }\n"
    "    return inner_no_upvar(base);\n"
    "}\n"
    "static int outer_rw_local(void) {\n"
    "    double v = 3.14;\n"
    "    void set_v(double val) { v = val; }\n"
    "    set_v(99.9);\n"
    "    return (v == 99.9) ? 1 : 0;\n"
    "}\n"
    "static int outer_multilevel(void) {\n"
    "    int g = 5;\n"
    "    int mid(int m) {\n"
    "        int inner_multilevel(int n) { return n + g; }\n"
    "        return inner_multilevel(m) * 2;\n"
    "    }\n"
    "    return mid(10);\n"
    "}\n"
    "static int outer_recursive(void) {\n"
    "    int fact(int n) {\n"
    "        if (n <= 1) return 1;\n"
    "        return n * fact(n - 1);\n"
    "    }\n"
    "    return fact(5);\n"
    "}\n"
    "static int outer_siblings(void) {\n"
    "    int a = 1, b = 2;\n"
    "    int get_a(void) { return a; }\n"
    "    int get_b(void) { return b; }\n"
    "    int sum_siblings(void) { return get_a() + get_b(); }\n"
    "    return sum_siblings();\n"
    "}\n"
    "int main(void) {\n"
    "    if (outer_no_upvar(21) != 42) return 1;\n"
    "    if (outer_rw_local() != 1) return 2;\n"
    "    if (outer_multilevel() != 30) return 3;\n"
    "    if (outer_recursive() != 120) return 4;\n"
    "    if (outer_siblings() != 3) return 5;\n"
    "    return 42;\n"
    "}\n"
)


def case_nested_fn_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  110: -c=native had no nested-function-aware call-site logic "
          "at all (grep -n is_nested src/serialize.c returned nothing) -- "
          "a genuine GNU nested function (Obj.is_nested, not an Apple "
          "block) is hoisted to file scope with a synthesized `void "
          "*__static_link` first parameter (the parser already puts a "
          "real param Obj there, parse_decl.c), but no call site ever "
          "passed it, and a reference to an enclosing function's local "
          "from inside a nested body printed as a bare identifier that "
          "doesn't exist at file scope (#1074). Fixed by lowering nested "
          "functions the same way Apple blocks already are (#965): one "
          "`struct __cccc_nenv_<name>` env struct per function that "
          "directly parents a nested function, an instance declared and "
          "populated at the top of its own body, every direct call "
          "passing the right env pointer as the static link (its own env "
          "for a direct child, a chase through ->__up -- mirroring "
          "codegen_expr.c's calling_nested walk exactly -- for a sibling "
          "or an ancestor's nested function), and an outer local/param "
          "reference rewritten to `(*env->__uvK)`. Exercises: no upvars "
          "at all; read+write of an outer *local* (not a global, which "
          "needs no static-link help); two-level nesting (the ->__up "
          "chase); recursion (forwarding one's own static link "
          "unchanged); and sibling nested functions calling each other. "
          "Asserts the -m output actually threads a static-link argument "
          "through, not just that the program happens to still run.")
    src = Path(tmp) / "nested_fn_1074.c"
    write(src, NESTED_FN_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "__static_link" not in m_result.stdout or "__cccc_nenv" not in m_result.stdout:
        print("    FAIL: -m output doesn't thread a static-link argument "
              "through nested-function call sites\n"
              f"    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "nested_fn_1074", NESTED_FN_PROGRAM)


STRUCT_BYVAL_PARAM_COPY_PROGRAM = (
    "struct s1078 { long a; long b; char pad[24]; };\n"
    "static void mutate(struct s1078 b) { b.a = 99; b.pad[0] = 1; }\n"
    "int main(void) {\n"
    "    struct s1078 s;\n"
    "    s.a = 1;\n"
    "    s.b = 2;\n"
    "    s.pad[0] = 0;\n"
    "    mutate(s);\n"
    "    return (s.a == 1 && s.pad[0] == 0) ? 42 : 7;\n"
    "}\n"
)


def case_struct_byval_param_copy(cccc: Path, tmp: str) -> bool:
    print("  111: the VM's own calling convention passed a struct/union "
          "by-value parameter as a raw pointer to the CALLER's own object, "
          "with no copy -- a write through the parameter inside the callee "
          "silently mutated the caller's argument (VM exit 7 on the repro "
          "below, wrong; every real host C compiler, and -c=native, copies "
          "the argument and would exit 42). This was also the actual root "
          "cause behind #1062's inverted premise (va_list forwarded as a "
          "parameter): the VM's aliasing behavior matched glibc's array-"
          "decay semantics, not macOS's genuine by-value va_list, the "
          "opposite of what #1062 assumed (#1078). Fixed in the callee's "
          "own prologue (gen_function, src/codegen_func.c): each struct/"
          "union param is copied into a fresh frame-local scratch slot "
          "(alloc_wide_bitint_temp's per-function pool, the same one "
          "vectors/decimals already use) via MCPY, and the param's own "
          "slot is rebound to point at the copy instead of the caller's "
          "object -- one site, no caller-side scratch-copy/CALLT-exclusion "
          "complexity needed. Asserts the VM alone now returns 42 (not "
          "just VM 42 -> native 42, since native was already correct).")
    src = Path(tmp) / "struct_byval_param_copy_1078.c"
    write(src, STRUCT_BYVAL_PARAM_COPY_PROGRAM)

    vm_result = run([str(cccc), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode} (parameter write "
              f"leaked back into the caller's object)\n    {vm_result.stderr}")
        return False

    return _native_run_case(cccc, tmp, "struct_byval_param_copy_1078",
                             STRUCT_BYVAL_PARAM_COPY_PROGRAM)


NESTED_FN_SHADOW_PROGRAM = (
    "int add(int a, int b) { return a + b; }\n"
    "int uses_outer_add(void) { return add(10, 5); }\n"
    "int main(void) {\n"
    "    int add(int a, int b) { return a + b + 1; }\n"
    "    if (add(40, 1) != 42) return 1;\n"
    "    if (uses_outer_add() != 15) return 2;\n"
    "    if (add(40, 2) != 43) return 3;\n"
    "    return 42;\n"
    "}\n"
)


def case_nested_fn_shadow_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  112: a nested (non-static) function DEFINITION whose name "
          "matched an enclosing file-scope function bound to -- and then "
          "\"redefined\" -- that outer function, instead of C block scope "
          "introducing a distinct declaration (C17 6.2.1p4: scope and "
          "linkage are separate axes; #1039 already established nested "
          "functions are implicitly static). function() (src/parse_decl.c) "
          "looked the name up via find_func(), which walks the ENTIRE "
          "enclosing scope chain -- correct for a bodyless block-scope "
          "prototype (#1056: external linkage, must bind to the outer "
          "function) but wrong for a nested definition, which must never "
          "merge with an outer Obj. Fixed by using "
          "find_func_in_current_scope() (already used for `static`) "
          "whenever a nested function DEFINITION is parsed, distinguished "
          "from a bodyless declaration the same way the existing early "
          "return already does (consume(';'), not equal(tok, '{'), to "
          "still catch a K&R definition -- #1043's lesson). A second, "
          "independent gap: once two same-named Objs could legally "
          "coexist, -c=native's hoisted-to-file-scope nested function "
          "(#1074) collided with the outer, non-static function of the "
          "same name -- rename_colliding_static_names() (src/serialize.c) "
          "only tracked `is_static` names and skipped every same-file "
          "collision on the now-stale assumption that one was always a "
          "parse-time redefinition error. Fixed by giving every "
          "non-static defining Obj's name an anchor entry, built in its "
          "own pass ahead of prog's own list order (a nested function's "
          "Obj is always pushed ahead of its enclosing function's own), "
          "that a same-named static/nested Obj must always yield to. "
          "Asserts the -m output actually renames the hoisted nested "
          "`add` rather than colliding with the outer one, in addition to "
          "the VM 42 -> native 42 round-trip.")
    src = Path(tmp) / "nested_fn_shadow_1075.c"
    write(src, NESTED_FN_SHADOW_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "__cccc_dup" not in m_result.stdout:
        print("    FAIL: -m output doesn't rename the hoisted nested "
              "`add` -- it collides with the outer, non-static `add`\n"
              f"    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "nested_fn_shadow_1075",
                                    NESTED_FN_SHADOW_PROGRAM)


F2I_NATIVE_PROGRAM = (
    "#include <fenv.h>\n"
    "int main(void) {\n"
    "    volatile double d_in_range = 1.5e19;\n"
    "    volatile double d_neg_inf = -1.0 / 0.0;\n"
    "    volatile float f_in_range = 1.5e19f;\n"
    "\n"
    "    feclearexcept(FE_ALL_EXCEPT);\n"
    "    unsigned long long u = (unsigned long long)d_in_range;\n"
    "    if (u != 15000000000000000000ULL) return 1;\n"
    "    if (fetestexcept(FE_INVALID)) return 2; // must NOT raise: in-range\n"
    "\n"
    "    long long r = (long long)d_neg_inf;\n"
    "    if (r != (-9223372036854775807LL - 1)) return 3; // saturates\n"
    "\n"
    "    unsigned long long uf = (unsigned long long)f_in_range;\n"
    "    if (uf != 15000000520515485696ULL) return 4;\n"
    "\n"
    "    return 42;\n"
    "}\n"
)


def case_f2i_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  113: -c=native's bare \"(dst_type)float_expr\" cast is UB in "
          "the host compiler for NaN/out-of-range values -- the VM defines "
          "it as saturating with FE_INVALID raised (F2I3/F2U3, #775/#780), "
          "but a real host compiler is free to do anything, and x86_64 "
          "clang/gcc's common branchless double/float->uint64 lowering "
          "does worse than \"anything\": it spuriously raises FE_INVALID "
          "for a proven in-range value (measured directly; aarch64's "
          "FCVTZS/FCVTZU already saturate correctly, so this case round-"
          "trips there even pre-fix and can't catch a regression on its "
          "own -- the -m text assertion below is what actually guards "
          "it). Fixed by routing every real-floating -> non-floating cast "
          "through one of four on-demand helpers "
          "(serialize_synth_f2i_helpers, src/serialize.c), near-verbatim "
          "ports of internal.h's own cccc_f64_to_i64/cccc_f32_to_i64/"
          "cccc_f64_to_u64/cccc_f32_to_u64, so native output agrees with "
          "the VM by construction (#1068). Asserts -m output calls "
          "__cccc_f2u64/__cccc_f2i64/__cccc_f2u64_f32, never a bare "
          "\"(unsigned long long)\"/\"(long long)\" cast of a float "
          "expression, plus VM 42 -> native 42.")
    src = Path(tmp) / "f2i_native_1068.c"
    write(src, F2I_NATIVE_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    for want in ("__cccc_f2u64(", "__cccc_f2i64(", "__cccc_f2u64_f32("):
        if want not in m_result.stdout:
            print(f"    FAIL: -m output missing '{want}'\n    {m_result.stdout}")
            return False

    return _vm_and_native_run_case(cccc, tmp, "f2i_native_1068", F2I_NATIVE_PROGRAM)


CTOR_DTOR_NATIVE_PROGRAM = (
    "int ctor_ran;\n"
    "int ctor_prio_ran;\n"
    "int order[1];\n"
    "int order_idx;\n"
    "__attribute__((constructor)) void plain_ctor(void) { ctor_ran = 1; }\n"
    "__attribute__((constructor(150))) void prio_ctor(void) { ctor_prio_ran = 1; }\n"
    "static __attribute__((constructor)) void static_ctor(void) { order[order_idx++] = 1; }\n"
    "__attribute__((destructor)) void plain_dtor(void) { (void)0; }\n"
    "int main(void) {\n"
    "    if (!ctor_ran) return 1;\n"
    "    if (!ctor_prio_ran) return 2;\n"
    "    if (order_idx != 1 || order[0] != 1) return 3;\n"
    "    return 42;\n"
    "}\n"
)


def case_ctor_dtor_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  114: __attribute__((constructor[(priority)]))/((destructor"
          "[(priority)])) was never lowered by serialize_function_"
          "signature() at all -- grep -n \"constructor\" src/serialize.c "
          "returned nothing but an unrelated comment, so under -c=native a "
          "marked function was emitted as an ordinary function nothing "
          "calls and simply never ran (no ordering bug -- no ordering at "
          "all). Fixed by emitting the attribute as a *prefix* on the "
          "declarator (not appended after it the way asm_label is: GCC "
          "rejects a trailing attribute on a function *definition*, clang "
          "accepts it -- the macOS-passes/Linux-fails shape this batch "
          "keeps relearning). Asserts -m output has "
          "__attribute__((constructor)) and __attribute__((constructor(150))) "
          "as prefixes on their declarators, plus VM 42 -> native 42 (#1020). "
          "Skipped under gcc on Darwin (SMOKE_CASE_SKIPS_GCC_MACOS, "
          "tools/testing/__init__.py) -- Homebrew gcc rejects the "
          "constructor(150) priority form outright, the same permanent "
          "WONT_FIX gap NATIVE_SKIP_TESTS_GCC_MACOS already quarantines for "
          "test_ctor_dtor_native_1020.c (#1196, #1193).")
    src = Path(tmp) / "ctor_dtor_native_1020.c"
    write(src, CTOR_DTOR_NATIVE_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    for want in ("__attribute__((constructor)) void plain_ctor",
                 "__attribute__((constructor(150))) void prio_ctor"):
        if want not in m_result.stdout:
            print(f"    FAIL: -m output missing '{want}'\n    {m_result.stdout}")
            return False

    return _vm_and_native_run_case(cccc, tmp, "ctor_dtor_native_1020", CTOR_DTOR_NATIVE_PROGRAM)


ATTR_AFTER_STDIO_PROGRAM = (
    "#include <stdio.h>\n"
    "int ctor_ran;\n"
    "int dtor_ran;\n"
    "__attribute__((constructor)) void ctor(void) { ctor_ran = 1; }\n"
    "__attribute__((destructor)) void dtor(void) { dtor_ran = 1; }\n"
    "int main(void) {\n"
    "    if (!ctor_ran) return 1;\n"
    "    return 42;\n"
    "}\n"
)


def case_attribute_survives_after_stdio_include(cccc: Path, tmp: str) -> bool:
    print("  115: CCCC's own include/Availability.h stub did `#define "
          "__attribute__(x)` (empty) unconditionally -- correct for CCCC's "
          "own preprocessing (its tokenizer parses __attribute__ as a "
          "builtin construct, never via macro expansion), but -c=native "
          "forwards -I./include to the real host cc verbatim, and a *real* "
          "preprocessor keeps that empty macro live for the rest of the "
          "translation unit. Once <stdio.h> pulled in sys/cdefs.h -> "
          "Availability.h, every later __attribute__(...) in the user's own "
          "TU silently vanished -- no error, no warning -- including the "
          "constructor/destructor attributes #1020 taught serialize.c to "
          "emit. Confirmed directly: `cc -I<repo>/include -E` on a plain "
          "__attribute__((noinline)) repro after #include <stdio.h> reduced "
          "the attribute to nothing. Fixed by guarding both "
          "include/Availability.h's own CCCC-flavored body and "
          "include/sys/cdefs.h's Availability.h include on #ifdef __CCCC__, "
          "handing off to the real host Availability.h (via "
          "__has_include_next) otherwise (#1083). Deliberately passes an "
          "explicit -I<repo>/include, like case 101 -- without it the "
          "replayed #include <stdio.h> wouldn't resolve through CCCC's own "
          "bundled headers at all and the case would pass vacuously. Makes "
          "the stripping *observable*: the constructor sets a flag main's "
          "return value depends on, so a stripped attribute means a "
          "non-42 exit, not a silently-identical binary.")
    src = Path(tmp) / "attr_after_stdio_1083.c"
    write(src, ATTR_AFTER_STDIO_PROGRAM)
    include_dir = cccc.parent / "include"

    vm_result = run([str(cccc), "-I", str(include_dir), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    out = Path(tmp) / "attr_after_stdio_1083_out"
    compile_result = run(
        [str(cccc), "-I", str(include_dir), "-c=native", "-o", out.name, src.name],
        cwd=tmp,
    )
    if compile_result.returncode != 0:
        print(f"    FAIL: native compile exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False

    print("    ok")
    return True


VA_LIST_PARAM_PROGRAM = (
    "#include <stdarg.h>\n"
    "static int consume_one(int n, va_list ap) {\n"
    "    (void)n;\n"
    "    return va_arg(ap, int);\n"
    "}\n"
    "static int forward(int count, ...) {\n"
    "    va_list ap;\n"
    "    va_start(ap, count);\n"
    "    int first = va_arg(ap, int);\n"
    "    int consumed = consume_one(1, ap);\n"
    "    int next = va_arg(ap, int);\n"
    "    va_end(ap);\n"
    "    if (first != 10) return 1;\n"
    "    if (consumed != 20) return 2;\n"
    "    if (next != 20) return 3;\n"
    "    return 0;\n"
    "}\n"
    "int main(void) {\n"
    "    return forward(2, 10, 20) == 0 ? 42 : 1;\n"
    "}\n"
)


def case_va_list_param_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  116: a va_list forwarded as an ordinary function *parameter* "
          "used to disagree between VM and native on glibc -- CCCC's own "
          "va_list is a plain struct, genuinely by-value on the VM (#1078), "
          "and matches by coincidence on macOS (a bare char *), but glibc's "
          "own va_list is `typedef struct __va_list_tag va_list[1]`, an "
          "array type that decays to a pointer in parameter position (C17 "
          "6.7.6.3p7) and aliases the caller's own va_list -- the callee's "
          "va_arg calls silently advanced the *caller's* va_list on glibc "
          "only, no build failure, no diagnostic (#1062). Fixed with a "
          "callee-side va_copy shim: the emitted parameter is renamed to "
          "__cccc_va_param_<name>, and the body gets an injected "
          "`va_list <name>; va_copy(<name>, __cccc_va_param_<name>);` so "
          "the body's own references restore by-value semantics on every "
          "host. Asserts -m output renames the parameter and carries the "
          "va_copy prologue, leaks none of reg_ptr/reg_count/stack_ptr, "
          "plus VM 42 -> native 42 (the aliasing itself is invisible on "
          "macOS by construction -- this only guards the emitted shape, "
          "the behavioral divergence needs a real glibc host, see "
          "tests/test_serialize_va_list_param_1062.c under the container).")
    src = Path(tmp) / "va_list_param_1062.c"
    write(src, VA_LIST_PARAM_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "va_list __cccc_va_param_ap" not in m_result.stdout:
        print(f"    FAIL: -m output missing renamed shim parameter\n    {m_result.stdout}")
        return False
    if "va_copy(ap, __cccc_va_param_ap)" not in m_result.stdout:
        print(f"    FAIL: -m output missing va_copy shim prologue\n    {m_result.stdout}")
        return False
    for leak in ("reg_ptr", "reg_count", "stack_ptr"):
        if leak in m_result.stdout:
            print(f"    FAIL: -m output leaks CCCC-internal va_list member '{leak}'\n    {m_result.stdout}")
            return False

    return _vm_and_native_run_case(cccc, tmp, "va_list_param_1062_rt", VA_LIST_PARAM_PROGRAM)


VA_LIST_LIBC_CALL_PROGRAM = (
    "#include <stdio.h>\n"
    "#include <stdarg.h>\n"
    "static int test_vsnprintf_basic(int count, ...) {\n"
    "    va_list ap;\n"
    "    va_start(ap, count);\n"
    "    int first = va_arg(ap, int);\n"
    "    char buf[32];\n"
    "    vsnprintf(buf, sizeof buf, \"%d\", ap);\n"
    "    int second = va_arg(ap, int);\n"
    "    va_end(ap);\n"
    "    if (first != 10) return 1;\n"
    "    if (buf[0] != '2' || buf[1] != '0' || buf[2] != 0) return 2;\n"
    "    if (second != 20) return 3;\n"
    "    return 0;\n"
    "}\n"
    "int main(void) {\n"
    "    return test_vsnprintf_basic(2, 10, 20) == 0 ? 42 : 1;\n"
    "}\n"
)


def case_va_list_libc_call_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  117: a va_list forwarded to a HOST LIBC v*-family function "
          "(vprintf/vsnprintf/vfprintf/vsscanf/vsyslog/...) used to "
          "disagree between VM and native on EVERY host, not just glibc -- "
          "the mirror bug to #1062 (a va_list forwarded as an ordinary "
          "function *parameter*), one layer further out (#1085). #1062's "
          "own filed premise ('the VM is always genuinely by-value') was "
          "backwards for this half: a struct/union by-value argument "
          "reaches an FFI cfunc by the caller's own address (#714), and the "
          "v*-family FFI wrappers (src/stdlib/format_printf.c and friends) "
          "had no prologue copy the way #1078 gives a CCCC-emitted callee -- "
          "they extracted straight out of the caller's own va_list struct, "
          "silently advancing it, on the VM on every platform. Fixed on the "
          "VM side by CCCC_VA_LOCAL (src/stdlib/va_ffi_helper.h), a "
          "snapshot-before-extract macro used at all twelve wrapper sites. "
          "Separately, -c=native genuinely does alias on glibc (real "
          "va_list array-decay, C17 6.7.6.3p7) when a va_list reaches ANY "
          "function taking one, host libc included -- fixed by wrapping any "
          "call passing a va_list-typed argument in a va_copy'd statement "
          "expression at the call site itself (serialize_expr's ND_FUNCALL "
          "case), not narrowed to bodiless callees only, so an indirect "
          "call through a function pointer is covered too. Asserts -m "
          "output wraps the vsnprintf call in "
          "'__extension__ ({ va_list __cccc_va_fwd' and leaks none of "
          "reg_ptr/reg_count/stack_ptr, plus VM 42 -> native 42 (the VM "
          "half of this bug is observable on macOS by construction, unlike "
          "#1062's own glibc-only residual -- see "
          "tests/test_serialize_va_list_libc_1085.c for the fuller case "
          "list, including the canonical measure-then-format idiom).")
    src = Path(tmp) / "va_list_libc_call_1085.c"
    write(src, VA_LIST_LIBC_CALL_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "__extension__ ({ va_list __cccc_va_fwd" not in m_result.stdout:
        print(f"    FAIL: -m output missing va_copy call-site shim\n    {m_result.stdout}")
        return False
    for leak in ("reg_ptr", "reg_count", "stack_ptr"):
        if leak in m_result.stdout:
            print(f"    FAIL: -m output leaks CCCC-internal va_list member '{leak}'\n    {m_result.stdout}")
            return False

    return _vm_and_native_run_case(cccc, tmp, "va_list_libc_call_1085_rt", VA_LIST_LIBC_CALL_PROGRAM)


PTHREAD_NATIVE_PROGRAM = (
    "#include <pthread.h>\n"
    "static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;\n"
    "_Thread_local int tls_val = 0;\n"
    "static void *worker(void *arg) {\n"
    "    (void)arg;\n"
    "    tls_val = 7;\n"
    "    pthread_mutex_lock(&mutex);\n"
    "    pthread_mutex_unlock(&mutex);\n"
    "    return (void *)(long)(tls_val == 7 ? 0 : 1);\n"
    "}\n"
    "int main(void) {\n"
    "    tls_val = 42;\n"
    "    pthread_t t;\n"
    "    if (pthread_create(&t, 0, worker, 0) != 0) return 1;\n"
    "    void *rc = 0;\n"
    "    pthread_join(t, &rc);\n"
    "    if ((long)rc != 0) return 2;\n"
    "    if (tls_val != 42) return 3;\n"
    "    return 42;\n"
    "}\n"
)


def case_pthread_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  118: include/pthread.h's own bundled pthread_mutex_t/"
          "pthread_cond_t were a VM-ABI struct-layout divergence, not a "
          "macro leak -- CCCC's own polyfill is a plain {void*,long,int} "
          "opaque-handle projection (24 bytes on macOS arm64), genuinely "
          "correct for the VM (the real host mutex is lazily heap-allocated "
          "on first lock, src/stdlib/pthread.c), but under -c=native the "
          "same 24-byte object got handed straight to the real host "
          "pthread_mutex_init() (which writes 64 bytes on macOS arm64) -- "
          "silent heap corruption, no compile/link error, flaking exit "
          "codes across repeated runs (#1022). Fixed by giving "
          "include/pthread.h a real #ifdef __CCCC__ / #include_next "
          "hand-off (#1021/#1040-style), a narrow PTHREAD_MUTEX_INITIALIZER/"
          "PTHREAD_COND_INITIALIZER re-emission fix (the real host struct "
          "has no .__handle/.__state/.__type members to designated-init), "
          "and emitting _Thread_local for Obj.is_tls (previously silently "
          "dropped -- every thread shared one instance instead of getting "
          "its own copy). Deliberately passes an explicit -I<repo>/include, "
          "like case 101/115 -- without it the replayed #include <pthread.h> "
          "wouldn't resolve through CCCC's own bundled header at all and "
          "the case would pass vacuously either way (the pre-#1022 bug only "
          "reproduces via the -I./include-forwarding path). Asserts -m "
          "output carries the bare PTHREAD_MUTEX_INITIALIZER macro name "
          "(not CCCC's own .__handle/.__state/.__type designators) and "
          "_Thread_local on tls_val, plus VM 42 -> native 42.")
    src = Path(tmp) / "pthread_native_1022.c"
    write(src, PTHREAD_NATIVE_PROGRAM)
    include_dir = cccc.parent / "include"

    vm_result = run([str(cccc), "-I", str(include_dir), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    m_result = run([str(cccc), "-I", str(include_dir), "-m", src.name], cwd=tmp)
    if "PTHREAD_MUTEX_INITIALIZER" not in m_result.stdout:
        print(f"    FAIL: -m output missing bare PTHREAD_MUTEX_INITIALIZER\n    {m_result.stdout}")
        return False
    if ".__handle" in m_result.stdout or ".__state" in m_result.stdout:
        print(f"    FAIL: -m output still leaks CCCC's own mutex projection\n    {m_result.stdout}")
        return False
    if "_Thread_local int tls_val" not in m_result.stdout:
        print(f"    FAIL: -m output missing _Thread_local on tls_val\n    {m_result.stdout}")
        return False

    out = Path(tmp) / "pthread_native_1022_out"
    compile_result = run(
        [str(cccc), "-I", str(include_dir), "-c=native", "-o", out.name, src.name],
        cwd=tmp,
    )
    if compile_result.returncode != 0:
        print(f"    FAIL: native compile exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False

    print("    ok")
    return True


BYVAL_MEMBER_ORDER_PROGRAM = """
typedef int (*EarlyFn)(struct Early *);

struct Early {
    int tag;
    long value;
};

struct Holder {
    char prefix[8];
    struct Early e;
};

static int use_early(struct Early *e) { return e->tag; }

int main(void) {
    EarlyFn fn = use_early;
    (void)fn;
    struct Holder h;
    h.e.tag = 7;
    h.e.value = 123456789L;
    if (h.e.tag != 7) return 1;
    if (h.e.value != 123456789L) return 2;
    return 42;
}
"""


def case_byval_member_order_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  119: a struct first reached only through a POINTER reference "
          "(e.g. a function-pointer typedef parameter, minilua's own "
          "lua_CFunction == int (*)(struct lua_State *)) pushed a then-"
          "incomplete Type into -c=native's ctx->defs at that early "
          "position; a BY-VALUE member of some other struct, collected in "
          "between, then needed the full body before it was available -- "
          "'field has incomplete type' (#1042a). The #1010 repromotion "
          "swap in collect_type() (src/serialize.c) fires once the real "
          "definition is reached but re-pushes at the TAIL of ctx->defs, "
          "after every entry collected since. Fixed with a stable "
          "topological reorder pass over ctx->defs: an edge for every "
          "by-value member forces its type's own definition ahead of its "
          "user, without disturbing any pair whose order was already "
          "legal. Asserts -m output prints 'struct Early {' before "
          "'struct Holder {', plus VM 42 -> native 42.")
    src = Path(tmp) / "byval_member_order_1042.c"
    write(src, BYVAL_MEMBER_ORDER_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    early_idx = m_result.stdout.find("struct Early {")
    holder_idx = m_result.stdout.find("struct Holder {")
    if early_idx < 0 or holder_idx < 0:
        print(f"    FAIL: -m output missing struct Early/Holder body\n    {m_result.stdout}")
        return False
    if early_idx > holder_idx:
        print(f"    FAIL: struct Early printed after struct Holder (wrong order)\n    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "byval_member_order_1042_rt",
                                    BYVAL_MEMBER_ORDER_PROGRAM)


OFFSETOF_ARRAY_LEN_PROGRAM = """
#include <stddef.h>

typedef struct {
    int a;
    long b;
    char c;
} Aux;

typedef union {
    long lastfree;
    char padding[offsetof(Aux, c)];
} Boxed;

int main(void) {
    if (sizeof(Boxed) != offsetof(Aux, c)) return 1;
    if (offsetof(Aux, c) <= sizeof(long)) return 2;
    return 42;
}
"""


def case_offsetof_array_len_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  120: offsetof(T, member)'s expansion (include/stddef.h: "
          "'(size_t)&(((type *)0)->member)') is a genuine integer constant "
          "expression per C11 6.6p9, but is_const_expr() "
          "(src/parse_analysis.c) had no ND_ADDR/ND_DEREF arm at all, so "
          "array_dimensions() (src/parse_types.c) misclassified "
          "'char padding[offsetof(T, m)]' as a VLA -- vla_of() (src/type.c) "
          "hard-codes VLA objects to size 8, a live VM sizeof bug "
          "independent of -c=native, and the VLA-length replay path "
          "(serialize_type_decl's TY_VLA case) re-emitted the raw "
          "expression -- including the referenced type's name -- with no "
          "forward-dependency tracking, 'use of undeclared identifier' "
          "(#1042d). Fixed with a structural (not 'whatever eval_rval() "
          "accepts', which also admits '&global') ND_ADDR arm: exactly "
          "ND_ADDR -> (ND_MEMBER|ND_DEREF)* -> ND_CAST-of-ND_NUM(0), so the "
          "member folds to a plain TY_ARRAY at parse time. Asserts -m "
          "output prints 'char padding[16];' (a plain integer, matching "
          "the union's real offsetof value on every 64-bit target this "
          "project supports), not a raw expression naming 'Aux' inside the "
          "array brackets, plus VM 42 -> native 42.")
    src = Path(tmp) / "offsetof_array_len_1042.c"
    write(src, OFFSETOF_ARRAY_LEN_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "char padding[16];" not in m_result.stdout:
        print(f"    FAIL: -m output missing folded 'char padding[16];'\n    {m_result.stdout}")
        return False
    if "padding[(" in m_result.stdout or "padding[&" in m_result.stdout:
        print(f"    FAIL: -m output still replays a raw expression as the array length\n    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "offsetof_array_len_1042_rt",
                                    OFFSETOF_ARRAY_LEN_PROGRAM)


STATIC_LIBC_COLLISION_PROGRAM = """
#include <stdio.h>

static int index(int base, int step) {
    return base + step * 2;
}

int main(void) {
    int r = index(10, 16);
    if (r != 42) return 1;
    return 42;
}
"""


def case_static_libc_collision_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  121: tests/test_minilua.c's own 'static int getmode(...)' is "
          "legal C in the source's own declaration order (its "
          "'#include <unistd.h>' comes AFTER the static definition -- a "
          "later, weaker declaration of an already-defined static doesn't "
          "redefine it) -- confirmed directly, clang -fsyntax-only on the "
          "real source compiles clean. -c=native's own #include-replay "
          "block hoists every captured include to the top of the output, "
          "unconditionally, ahead of every prototype/definition, "
          "inverting that legal order and manufacturing a collision "
          "against macOS libc's real getmode() the user's program never "
          "actually has (#1042c). Fixed with a host-symbol probe added to "
          "rename_colliding_static_names() (src/serialize.c, #1002): any "
          "static, defining Obj whose name resolves in the host libc's own "
          "symbol namespace -- probed via dlsym on the SAME handle "
          "cc_load_libc()/find_libc() already use, deliberately never "
          "RTLD_DEFAULT/dlopen(NULL) (those also see the compiler process "
          "itself, making output depend on which cccc binary ran it) -- "
          "gets the pass's existing '%s__cccc_dupN' rename. Gated on the "
          "program actually replaying at least one real #include to the "
          "host at all (any_real_include_replayed(), src/serialize.c) -- "
          "found necessary after a Linux/glibc 2.39 regression where a "
          "-c=native-only, never-replayed CCCC polyfill header's own "
          "re-derived function collided with a newer real glibc symbol of "
          "the same name with zero actual hazard, since nothing ever "
          "declared it to the host in that case. This program's "
          "'#include <stdio.h>' exists purely to satisfy that gate. "
          "'index' (a legacy BSD function CCCC's own bundled string.h does "
          "not declare, unlike getmode/<unistd.h>) is used as the "
          "colliding name since it exists on every host this project "
          "supports. Asserts -m output renames the static to "
          "'index__cccc_dup', not a bare 'index' definition, plus "
          "VM 42 -> native 42.")
    src = Path(tmp) / "static_libc_collision_1042.c"
    write(src, STATIC_LIBC_COLLISION_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "index__cccc_dup" not in m_result.stdout:
        print(f"    FAIL: -m output did not rename the colliding static 'index'\n    {m_result.stdout}")
        return False
    if "static int index(int base" in m_result.stdout:
        print(f"    FAIL: -m output still prints the unrenamed 'index' definition\n    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "static_libc_collision_1042_rt",
                                    STATIC_LIBC_COLLISION_PROGRAM)


LAYOUT_CONST_PROGRAM = """
#include <pthread.h>

extern void *malloc(unsigned long size);
extern void  free(void *ptr);
extern void *memset(void *s, int c, unsigned long n);

int main(void) {
    unsigned long guest_size = sizeof(pthread_mutex_t);
    unsigned long align      = _Alignof(pthread_mutex_t);
    if (align == 0 || (align & (align - 1)) != 0)
        return 1;

    unsigned long  tail = 64;
    unsigned char *buf  = (unsigned char *)malloc(guest_size + tail);
    if (!buf)
        return 2;
    memset(buf, 0, guest_size);
    memset(buf + guest_size, 0xAA, tail);

    pthread_mutex_t *m = (pthread_mutex_t *)buf;
    if (pthread_mutex_init(m, 0) != 0) {
        free(buf);
        return 3;
    }
    if (pthread_mutex_lock(m) != 0) {
        pthread_mutex_destroy(m);
        free(buf);
        return 4;
    }
    if (pthread_mutex_unlock(m) != 0) {
        pthread_mutex_destroy(m);
        free(buf);
        return 5;
    }
    pthread_mutex_destroy(m);

    for (unsigned long i = guest_size; i < guest_size + tail; i++) {
        if (buf[i] != 0xAA) {
            free(buf);
            return 6;
        }
    }
    free(buf);

    unsigned long combined = sizeof(pthread_mutex_t) + 8;
    if (combined != guest_size + 8)
        return 7;

    return 42;
}
"""


def case_layout_const_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  122: -c=native re-materializes sizeof/_Alignof of a "
          "from_include type (e.g. pthread_mutex_t, struct statfs) "
          "textually instead of emitting guest-side folded literal, so "
          "the host header's own real layout wins on both the member-"
          "access side (already correct) and the sizeof/_Alignof side "
          "(#1031). Asserts -m output prints 'sizeof(pthread_mutex_t)' "
          "rather than a bare folded literal (e.g. '24ULL'), plus "
          "VM 42 -> native 42 -- pre-fix, the native run's real "
          "pthread_mutex_init()/lock() overran a buffer malloc'd off the "
          "stale guest-folded size (see tests/test_serialize_layout_"
          "const_1031.c's own pre-fix-verified canary).")
    src = Path(tmp) / "layout_const_1031.c"
    write(src, LAYOUT_CONST_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "sizeof(pthread_mutex_t)" not in m_result.stdout:
        print(f"    FAIL: -m output missing re-materialized "
              f"'sizeof(pthread_mutex_t)'\n    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "layout_const_1031_rt",
                                    LAYOUT_CONST_PROGRAM)


STATIC_LABEL_TABLE_PROGRAM = """
static int dispatch(int idx) {
    static const void *tab[] = {&&zero, &&ten, &&twenty};
    goto                    *tab[idx];
zero:
    return 0;
ten:
    return 10;
twenty:
    return 20;
}

int main(void) {
    return dispatch(0) + dispatch(1) + dispatch(2) + 12;
}
"""


def case_static_label_table_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  123: a function-local `static` initialized with a label's "
          "address ([GNU] labels-as-values, `&&label`) used to hard-error "
          "under -c=native ('cannot serialize initializer for global "
          "... unresolved relocation target') -- rename_anon_globals() "
          "hoists such a static to file scope like any other anonymous "
          "global, but a label's address has no C spelling there, only "
          "inside the function that defines the label (verified directly "
          "against real clang/GCC, which both accept the identical idiom "
          "as long as it stays function-local) (#1044). Fixed by deferring "
          "such a global's real definition into its owning function's own "
          "body (collect_deferred_static_labels(), src/serialize.c) "
          "instead of hoisting it. Asserts -m output declares the static "
          "table inside dispatch()'s own body (not at file scope) with its "
          "`&&label` initializers intact, never the old diagnostic, plus "
          "VM 42 -> native 42 -- this was the sole remaining blocker for "
          "tests/test_minilua.c's own computed-goto dispatch table.")
    src = Path(tmp) / "static_label_table_1044.c"
    write(src, STATIC_LABEL_TABLE_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "unresolved relocation" in m_result.stdout + m_result.stderr:
        print(f"    FAIL: -m still hard-errors on the label relocation\n"
              f"    {m_result.stdout}{m_result.stderr}")
        return False
    if "&&zero" not in m_result.stdout:
        print(f"    FAIL: -m output missing '&&zero' initializer\n"
              f"    {m_result.stdout}")
        return False
    # The deferred definition must be indented (inside dispatch()'s body),
    # not sitting at file-scope column 0 the way an ordinary hoisted
    # anonymous global would.
    if "\nstatic const void *__cccc_tab_0" in m_result.stdout:
        print(f"    FAIL: static table was hoisted to file scope, not kept "
              f"inside dispatch()'s own body\n    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "static_label_table_1044_rt",
                                    STATIC_LABEL_TABLE_PROGRAM)


CLOSE_NO_INCLUDE_DIR_PROGRAM = (
    "#include <fcntl.h>\n"
    "extern int close(int fd);\n"
    "int main(void) {\n"
    "    int f = 0;\n"
    "    if (f)\n"
    "        f = close(1);\n"
    "    return 42;\n"
    "}\n"
)


def case_bundled_header_bodiless_decl_no_include_dir(cccc: Path, tmp: str) -> bool:
    print("  124: a bodiless `extern int close(int);` written in a primary "
          "source file was silently dropped from -c=native/-m output "
          "whenever the file also #include's a CCCC-bundled header -- "
          "bundled fcntl.h's own `#include \"unistd.h\"` (the header that "
          "actually declares close()) made the #901 bodiless-declaration "
          "gate think the auto-captured `#include <fcntl.h>` already "
          "supplied it, but the replayed #include resolves to the HOST's "
          "own fcntl.h under -c=native, which never declares close() -- "
          "'use of undeclared identifier close' (#1096). This case runs "
          "with no -I./include (unlike this file's own default cccc, "
          "which always has a repo ./include alongside it) so the "
          "embedded src/std.c header table resolves fcntl.h, exactly the "
          "invocation shape a copied binary with no include/ directory "
          "hits -- the test harness's own tools/testing/native.py:87 "
          "always passes -I./include, which is what let this ship "
          "unnoticed. Fixed via Compiler.cccc_bundled_files "
          "(cc_file_is_cccc_bundled()/mark_cccc_bundled_file(), "
          "src/preprocess.c): a declaration sourced from CCCC's own "
          "bundled header chain (not a real host header reached "
          "transitively, which genuinely IS supplied once its own "
          "#include replays) is emitted when its own header was never "
          "itself replayed, gated on obj->is_used so an unrelated "
          "unistd.h declaration the program never calls isn't also "
          "dumped. Asserts -m output declares close(), plus VM 42 -> "
          "native 42 with no -I./include on the compile step.")
    src = Path(tmp) / "close_no_include_dir_1096.c"
    write(src, CLOSE_NO_INCLUDE_DIR_PROGRAM)

    # Deliberately no -I./include here -- see the case's own printed
    # rationale. -m alone is enough to observe the emitted declaration.
    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "close(" not in m_result.stdout:
        print(f"    FAIL: -m output missing a 'close' prototype\n"
              f"    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "close_no_include_dir_1096_rt",
                                    CLOSE_NO_INCLUDE_DIR_PROGRAM)


LAYOUT_CONST_SITES_PROGRAM = """
#include <sys/mount.h>

// A tagged enum with a variable actually declared of that type forces its
// own body to be serialized -- a tagless enum used only for its constant's
// VALUE (N below would otherwise never need its type's own definition
// emitted at all) wouldn't exercise serialize_enum_def()'s own
// re-materialization, only the propagated-to-every-USE half.
enum Layout1095 { N = sizeof(struct statfs) };
static enum Layout1095 g_enum_dummy;

static char g_init[sizeof(struct statfs)] = {1};

int main(void) {
    char buf[sizeof(struct statfs)];
    (void)buf;

    unsigned long n = sizeof(struct statfs);
    switch (n) {
        case sizeof(struct statfs):
            break;
        default:
            return 1;
    }

    if (N != (int)sizeof(struct statfs))
        return 2;

    (void)g_init;
    (void)g_enum_dummy;
    return 42;
}
"""


def case_layout_const_sites_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  125: #1031's own residual -- a sizeof/_Alignof of a "
          "from_include type re-materializes textually under -c=native "
          "only when it survives as a bare expression node by the time "
          "serialize_expr() runs; every other const_expr()/eval() "
          "consumer kept only the folded int64_t and stayed folded "
          "against CCCC's own (possibly stale) guest projection (#1095). "
          "Closes three of those sites, sharing serialize_layout_const() "
          "(factored out of #1031's own ND_NUM arm) rather than a "
          "parallel copy: array dimensions (a LOCAL or an uninitialized "
          "global only -- an initialized global's dimension stays folded "
          "on purpose, since serialize_init_bytes' own byte image is "
          "still sized off the folded value and the two must not "
          "disagree), case labels, and enum values (propagated to every "
          "USE of the enumerator too, not just its own declaration in "
          "the enum body, via VarScope.enum_layout_ty -- and NOT "
          "propagated to a later auto-incrementing enumerator that "
          "depends on this one's value, which stays folded instead, the "
          "same 'leave the inconsistent case folded' rule as the "
          "initialized-global exclusion). Bitfield widths and an "
          "initialized global's byte image remain open (#1099, WONT_FIX "
          "-- see man/COVERAGE.md's own entry for why those two are "
          "actively unsound to fix the same way, not merely deferred); "
          "_Static_assert re-emission is #1098, a separate case below. "
          "Asserts -m output prints "
          "'buf[sizeof(struct statfs)]', 'case sizeof(struct statfs)', "
          "and 'N = sizeof(struct statfs)' rather than a folded literal "
          "in each of the three sites, that the initialized global's own "
          "dimension is DELIBERATELY still folded (the negative half of "
          "this case), plus VM 42 -> native 42.")
    src = Path(tmp) / "layout_const_sites_1095.c"
    write(src, LAYOUT_CONST_SITES_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = m_result.stdout
    if "buf[sizeof(struct statfs)]" not in out:
        print(f"    FAIL: -m output missing re-materialized array "
              f"dimension\n    {out}")
        return False
    if "case sizeof(struct statfs)" not in out:
        print(f"    FAIL: -m output missing re-materialized case label\n"
              f"    {out}")
        return False
    if "N = sizeof(struct statfs)" not in out:
        print(f"    FAIL: -m output missing re-materialized enum value\n"
              f"    {out}")
        return False
    # Negative half: an initialized global's own array dimension must stay
    # folded -- its emitted byte image (serialize_init_bytes) is still
    # sized off the folded value, so re-materializing only the dimension
    # would make the two disagree (#1095's own scoping decision).
    if "g_init[sizeof(struct statfs)]" in out:
        print(f"    FAIL: an INITIALIZED global's dimension was "
              f"re-materialized -- must stay folded\n    {out}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "layout_const_sites_1095_rt",
                                    LAYOUT_CONST_SITES_PROGRAM)


STATIC_ASSERT_PROGRAM = """
#include <sys/mount.h>

// #1098: the file-scope form. Deliberately `>=`, not `==` -- true against
// BOTH CCCC's own guest projection of struct statfs and the real host's
// (much larger) one, so this exercises the RE-EMISSION itself rather than
// a projection-vs-real-layout mismatch (see test_serialize_static_assert_
// 1098.c for that half, and dbg5-style manual verification in the ticket
// for a deliberately-failing `==` case).
_Static_assert(sizeof(struct statfs) >= 8, "struct statfs too small");

int main(void) {
    // Block-scope form, same gate.
    static_assert(sizeof(struct statfs) >= 8, "struct statfs too small (block)");

    // Negative half: an ordinary compile-time-only assert (no from_include
    // type anywhere in its condition) must NOT be re-emitted -- confirms
    // the host-owned-layout gate (expr_has_host_owned_layout(),
    // serialize.c) doesn't fire indiscriminately on every static_assert in
    // a TU that merely includes a header with from_include types in scope.
    _Static_assert(sizeof(int) == 4, "int must be 4 bytes");

    return 42;
}
"""


def case_static_assert_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  126: #1098 -- a `_Static_assert`/`static_assert` is evaluated "
          "against CCCC's own type projection at parse time; a failing "
          "assertion is a compile error, so only a passing one ever "
          "reaches the serializer, which never emitted `_Static_assert` "
          "at all -- a host whose real layout would fail the same check "
          "compiled anyway. -c=native now re-emits the assert (both file- "
          "and block-scope forms) for the host to re-check, gated on the "
          "condition actually depending on a host-owned from_include "
          "struct/union layout (expr_has_host_owned_layout(), narrower "
          "than type_layout_is_host_owned() itself to sidestep a scalar-"
          "typedef same_type_or_origin() false-positive -- see that "
          "function's own comment) AND the assert being written in a "
          "command-line input file (the #901/#1096 provenance gate), so "
          "one of CCCC's own bundled headers' own per-platform layout "
          "asserts (include/sys/stat.h, signal.h, fts.h, aio.h, etc.) is "
          "never re-emitted against the wrong host. Asserts -m output "
          "contains both re-emitted asserts with their message text "
          "intact, does NOT contain the ordinary sizeof(int)==4 assert, "
          "plus VM 42 -> native 42.")
    src = Path(tmp) / "static_assert_1098.c"
    write(src, STATIC_ASSERT_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = m_result.stdout
    if "_Static_assert((unsigned long)8 <= sizeof(struct statfs), " \
       "\"struct statfs too small\")" not in out:
        print(f"    FAIL: -m output missing re-materialized file-scope "
              f"_Static_assert\n    {out}")
        return False
    if "_Static_assert((unsigned long)8 <= sizeof(struct statfs), " \
       "\"struct statfs too small (block)\")" not in out:
        print(f"    FAIL: -m output missing re-materialized block-scope "
              f"_Static_assert\n    {out}")
        return False
    if "sizeof(int)" in out and "int must be 4 bytes" in out:
        print(f"    FAIL: an ordinary compile-time-only assert (no "
              f"from_include type) was re-emitted -- must stay "
              f"unemitted\n    {out}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "static_assert_1098_rt",
                                    STATIC_ASSERT_PROGRAM)


SIZEOF_ONLY_AGGREGATE_1167_PROGRAM = """
#include <sys/mount.h>

// #1167: a struct/union/enum referenced ONLY inside sizeof/_Alignof/a case
// label/an enum value/a _Static_assert is const-folded to a plain integer
// literal at parse time -- collect_node_types()/collect_type()
// (serialize_type.c) never walked the operand type's own layout-provenance
// stash (Node.layout_ty and friends), so no AST node it DID walk still
// referenced it, no definition was emitted, and the host compiler rejected
// the re-materialized `sizeof(T)`/`_Alignof(T)` text as an incomplete type.
// Every struct below is deliberately referenced in exactly one of these
// five sites and nowhere else.

// Site 1: bare sizeof expression -- the ticket's own minimal repro shape
// (a plain, non-host-owned aggregate).
struct SizeOnly1167 { long a; double b; char c[3]; };

// Site 2: a LOCAL array dimension (#1095's own array-dimension gate only
// re-materializes a local's or an uninitialized global's own declarator).
struct ArrOnly1167 { int x; long y; };

// Site 3: a `case` label.
struct CaseOnly1167 { char a; int b; };

// Site 4: an enum value.
struct EnumOnly1167 { long a; long b; long c; };

// #1167: legitimately host-owned (wraps a from_include aggregate,
// `struct statfs`), referenced only via sizeof -- distinct from the
// spurious scalar-vs-from_include-typedef gate misfire that
// type_layout_is_host_owned() can also trigger (a separate, out-of-scope
// defect); this one really is host-owned per that function's own by-value
// -member walk, so it must both re-materialize AND actually get a real
// definition emitted.
struct WrapStatfs1167 { struct statfs s; };

// #1167 regression guard: a member type with NO native/-m lowering at all
// (`_BitInt(129)` -- serialize_type.c's own TY_BITINT arm hard-errors on
// it), referenced only via sizeof, and NOT host-owned -- must stay folded
// (never actually emitted), exactly as it did before this fix. An earlier,
// unconditional version of this fix collected every layout_ty regardless
// of whether serialize_layout_const() would ever re-materialize it,
// forcing this struct's body into the output and hitting that hard error
// even though nothing about it is host-owned (caught by
// tests/suites/test_suite_structs.c's own `tc_bi1135_wide` under
// `python3 tools/tests.py --native`).
struct WideBitintOnly1167 { char c; _BitInt(129) x; };

// Site 5: _Static_assert, non-host-owned, both file- and block-scope.
struct AssertOnly1167 { long a; long b; };
_Static_assert(sizeof(struct AssertOnly1167) == sizeof(struct AssertOnly1167),
               "file-scope AssertOnly1167");

int main(void) {
    long sizeof_expr = (long)sizeof(struct SizeOnly1167);

    char arr[sizeof(struct ArrOnly1167)];
    (void)arr;

    int v = (int)sizeof(struct CaseOnly1167);
    switch (v) {
        case sizeof(struct CaseOnly1167):
            break;
        default:
            return 1;
    }

    enum { N1167 = sizeof(struct EnumOnly1167) };
    if (N1167 <= 0)
        return 2;

    long wrap_sz   = (long)sizeof(struct WrapStatfs1167);
    long bitint_sz = (long)sizeof(struct WideBitintOnly1167);

    static_assert(sizeof(struct AssertOnly1167) ==
                       sizeof(struct AssertOnly1167),
                   "block-scope AssertOnly1167");

    if (sizeof_expr > 0 && wrap_sz > 0 && bitint_sz > 0)
        return 42;
    return 3;
}
"""


def case_sizeof_only_aggregate_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  132: #1167 -- a struct/union/enum referenced ONLY inside "
          "sizeof/_Alignof/a case label/an enum value/a _Static_assert is "
          "const-folded to a plain integer literal at parse time, but the "
          "operand Type is retained on the fold site (Node.layout_ty and "
          "friends, #1031/#1095/#1098) -- collect_node_types()/"
          "collect_type() never walked those stashes, so a re-materialized "
          "`sizeof(T)`/`_Alignof(T)` (serialize_layout_const()) could be "
          "the only reference to T left in the whole AST, and no "
          "definition was emitted for it ('invalid application of "
          "sizeof to an incomplete type'). Fixed by walking "
          "Node.layout_ty/case_begin_layout_ty/case_end_layout_ty, "
          "Type.array_len_layout_ty, EnumConstant.layout_ty, and file-scope "
          "StaticAssertRecord.cond, each gated identically to "
          "serialize_layout_const()'s own re-materialization check "
          "(layout_type_needs_collecting()) so a type that stays folded is "
          "never force-emitted -- confirmed against the regression that "
          "gate exists for (WideBitintOnly1167, see its own comment). "
          "Covers all five sites plus a genuinely host-owned aggregate. "
          "Asserts VM 42 -> native 42.")
    return _vm_and_native_run_case(cccc, tmp, "sizeof_only_aggregate_1167",
                                    SIZEOF_ONLY_AGGREGATE_1167_PROGRAM)


SCALAR_NOT_HOST_OWNED_1168_PROGRAM = """
#include <sys/mount.h>

// #1168: type_layout_is_host_owned() used to accept any Type kind, not
// just struct/union/enum -- a bare scalar member/operand (e.g. plain
// `long`) could spuriously same_type_or_origin()-match an unrelated
// from_include *typedef* of the same builtin (reached merely by including
// some header) via the origin-chain pointer-identity walk, judging an
// entirely ordinary, non-host-owned aggregate "host-owned" and
// re-materializing its sizeof textually instead of folding it.

// The ticket's own minimal repro shape: an ordinary user struct with only
// scalar members, nothing from_include anywhere in it.
struct Loc1168 { long a; double b; };

// A genuinely host-owned wrapper (contains a from_include struct member)
// must still re-materialize -- this case must not overcorrect.
struct Wrap1168 { struct statfs s; };

int main(void) {
    unsigned long loc_sz  = sizeof(struct Loc1168);
    unsigned long int_sz  = sizeof(int);
    unsigned long wrap_sz = sizeof(struct Wrap1168);

    if (loc_sz != sizeof(long) + sizeof(double) &&
        loc_sz != sizeof(double) + sizeof(long))
        return 1;
    if (int_sz != sizeof(int))
        return 2;
    if (wrap_sz == 0)
        return 3;

    return 42;
}
"""


def case_scalar_member_not_host_owned_native_round_trip(cccc: Path,
                                                          tmp: str) -> bool:
    print("  135: #1168 -- type_layout_is_host_owned() accepted any Type "
          "kind (its own doc comment always said 'struct/union', but the "
          "code didn't enforce it), so a plain scalar member/operand (e.g. "
          "`long`) could spuriously same_type_or_origin()-match an "
          "unrelated from_include typedef of the same builtin (e.g. "
          "sys/types.h's __int32_t, reached merely by including a header) "
          "via the origin-chain pointer-identity walk -- judging an "
          "ordinary, entirely user-defined aggregate host-owned and "
          "re-materializing `sizeof(struct Loc1168)` textually instead of "
          "folding it to a literal, even though the struct has nothing to "
          "do with any header. Fixed by restricting "
          "type_layout_is_host_owned() to TY_STRUCT/TY_UNION/TY_ENUM, "
          "mirroring the narrowing #1098's expr_has_host_owned_layout() "
          "already applied to itself for the same reason. Asserts -m "
          "output folds `sizeof(struct Loc1168)`/`sizeof(int)` to plain "
          "literals (does NOT contain 'sizeof(struct Loc1168)'), while a "
          "genuinely host-owned wrapper (Wrap1168, containing a "
          "from_include struct member) still re-materializes as "
          "`sizeof(struct Wrap1168)` -- plus VM 42 -> native 42.")
    src = Path(tmp) / "scalar_not_host_owned_1168.c"
    write(src, SCALAR_NOT_HOST_OWNED_1168_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "sizeof(struct Loc1168)" in m_result.stdout:
        print(f"    FAIL: -m output re-materializes non-host-owned "
              f"'sizeof(struct Loc1168)' instead of folding it\n"
              f"    {m_result.stdout}")
        return False
    if "sizeof(struct Wrap1168)" not in m_result.stdout:
        print(f"    FAIL: -m output missing re-materialized "
              f"'sizeof(struct Wrap1168)' (genuinely host-owned)\n"
              f"    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "scalar_not_host_owned_1168",
                                    SCALAR_NOT_HOST_OWNED_1168_PROGRAM)


SCALAR_HOST_DIVERGENT_1169_PROGRAM = """
#include <signal.h>
#include <stdarg.h>

// #1169, follow-up to #1168: type_layout_is_host_owned() restricted itself
// to TY_STRUCT/TY_UNION/TY_ENUM to fix #1168's spurious-scalar-member false
// positive, but that also stopped a from_include *scalar* typedef whose
// real host layout genuinely differs (sigset_t: unsigned int/4 bytes in
// CCCC's own include/signal.h, 128 bytes on glibc) from ever
// re-materializing -- reintroducing the #1031 hazard for it. Fixed via
// find_typedef_name_exact()'s pointer-identity lookup: parse_typedef()
// already copy_type()s every non-aggregate typedef, so sigset_t has its own
// Type identity distinct from the bare `unsigned int` it aliases -- no
// structural (same_type_or_origin()) match needed, so #1168's bug can't
// reopen.

// Reached through an aggregate member too (this ticket's chosen scope).
struct Wrap1169 { sigset_t s; };

int main(void) {
    unsigned long sigset_sz = sizeof(sigset_t);
    unsigned long wrap_sz   = sizeof(struct Wrap1169);
    unsigned long long_sz   = sizeof(long);
    unsigned long uint_sz   = sizeof(unsigned int);
    unsigned long va_sz     = sizeof(va_list);

    if (sigset_sz == 0)
        return 1;
    if (wrap_sz < sigset_sz)
        return 2;
    if (long_sz != sizeof(long))
        return 3;
    if (uint_sz != sizeof(unsigned int))
        return 4;
    if (va_sz != sizeof(va_list))
        return 5;

    return 42;
}
"""


def case_scalar_host_divergent_native_round_trip(cccc: Path,
                                                   tmp: str) -> bool:
    print("  136: #1169, follow-up to #1168 -- type_layout_is_host_owned() "
          "restricting itself to TY_STRUCT/TY_UNION/TY_ENUM fixed #1168's "
          "spurious-scalar-member false positive, but collaterally stopped "
          "a from_include *scalar* typedef whose real host layout genuinely "
          "differs (sigset_t: unsigned int/4 bytes in CCCC's own "
          "include/signal.h, 128 bytes on glibc) from ever "
          "re-materializing, reintroducing the #1031 hazard for it. Fixed "
          "by keying a scalar arm on find_typedef_name_exact()'s "
          "pointer-identity lookup instead of same_type_or_origin()'s "
          "structural fallback -- parse_typedef() already copy_type()s "
          "every non-aggregate typedef, giving sigset_t its own Type "
          "identity distinct from the bare `unsigned int` it aliases, so "
          "#1168's spurious-match bug can't reopen. Asserts -m output "
          "re-materializes `sizeof(sigset_t)` (both as a direct operand and "
          "reached through struct Wrap1169's own member), a bare "
          "`sizeof(long)`/`sizeof(unsigned int)` stays folded (#1168 must "
          "not regress), and `sizeof(va_list)` still folds to its "
          "compiler-owned safe-upper-bound literal (the va_list/jmp_buf "
          "carve-out, type_header_is_compiler_owned(), is unaffected) -- "
          "plus VM 42 -> native 42. `sizeof(struct Wrap1169)` (containing a "
          "sigset_t member) re-materializes too -- as its own type name, "
          "not textually as `sizeof(sigset_t)` -- proving the member "
          "recursion half of the fix. The exit code itself can't assert a "
          "specific byte count for sizeof(sigset_t), since it legitimately "
          "differs between the VM and a native host's own libc -- the -m "
          "text assertions below are the real regression guard.")
    src = Path(tmp) / "scalar_host_divergent_1169.c"
    write(src, SCALAR_HOST_DIVERGENT_1169_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "sizeof(sigset_t)" not in m_result.stdout:
        print(f"    FAIL: -m output missing re-materialized "
              f"'sizeof(sigset_t)'\n"
              f"    {m_result.stdout}")
        return False
    # struct Wrap1169's own sizeof re-materializes as its OWN type name
    # (`sizeof(struct Wrap1169)`), not textually as `sizeof(sigset_t)` --
    # the member-recursion half of type_layout_is_host_owned() only decides
    # WHETHER Wrap1169 counts as host-owned, not what name gets printed.
    if "sizeof(struct Wrap1169)" not in m_result.stdout:
        print(f"    FAIL: -m output should re-materialize "
              f"'sizeof(struct Wrap1169)' too -- its sigset_t member should "
              f"make the enclosing struct host-owned (the member-recursion "
              f"half of #1169's chosen scope)\n"
              f"    {m_result.stdout}")
        return False
    if "sizeof(long)" in m_result.stdout:
        print(f"    FAIL: -m output re-materializes a bare 'sizeof(long)' "
              f"instead of folding it (#1168 regression)\n"
              f"    {m_result.stdout}")
        return False
    if "sizeof(unsigned int)" in m_result.stdout:
        print(f"    FAIL: -m output re-materializes a bare "
              f"'sizeof(unsigned int)' instead of folding it (#1168 "
              f"regression)\n"
              f"    {m_result.stdout}")
        return False
    if "sizeof(va_list)" in m_result.stdout:
        print(f"    FAIL: -m output re-materializes 'sizeof(va_list)' "
              f"instead of folding it to its compiler-owned safe upper "
              f"bound\n"
              f"    {m_result.stdout}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "scalar_host_divergent_1169",
                                    SCALAR_HOST_DIVERGENT_1169_PROGRAM)


BLOCK_IN_NESTED_1080_PROGRAM = (
    "int ticket_repro(void) {\n"
    "    int g = 7;\n"
    "    int mid(int m) {\n"
    "        int (^blk)(void) = ^{ return g + m; };\n"
    "        return blk();\n"
    "    }\n"
    "    return mid(3);\n"
    "}\n"
    "int block_var_ancestor(void) {\n"
    "    __block int g = 0;\n"
    "    int mid(int m) {\n"
    "        void (^blk)(void) = ^{ g += m; };\n"
    "        blk();\n"
    "        return 0;\n"
    "    }\n"
    "    mid(4);\n"
    "    return g;\n"
    "}\n"
    "int main(void) {\n"
    "    if (ticket_repro() != 10) return 1;\n"
    "    if (block_var_ancestor() != 4) return 2;\n"
    "    return 42;\n"
    "}\n"
)


def case_block_in_nested_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  127: -c=native rejected a block literal inside a genuinely "
          "nested function capturing a variable owned by one of that "
          "function's own ancestors ('a block literal inside a nested "
          "function capturing ... is not supported (#1074 follow-up)') -- "
          "the VM-side miscompile itself was fixed by #1076, but the "
          "native-lowering follow-up (#1080) was never done: "
          "collect_nested_refs()'s ND_BLOCK_LITERAL arm now registers the "
          "ancestor-owned capture as an upvar of the real owner "
          "(record_nested_upvar()) instead of rejecting it, and the "
          "capture-copy loop in serialize_expr()'s ND_BLOCK_LITERAL case "
          "reads it back through the same env chase (nested_env_ptr_expr) "
          "an ordinary nested-function upvar reference uses. A "
          "__block-storage ancestor capture (previously rejected "
          "outright) is supported too, via an extra level of indirection "
          "in the env field (T ** instead of T *). Asserts VM 42 -> "
          "native 42 (both cases previously a hard compile-time rejection "
          "under -c=native).")
    return _vm_and_native_run_case(cccc, tmp, "block_in_nested_1080",
                                    BLOCK_IN_NESTED_1080_PROGRAM)


NESTED_FN_IN_BLOCK_1081_PROGRAM = (
    "int ticket_repro(void) {\n"
    "    int g = 7;\n"
    "    int (^blk)(int) = ^(int m) {\n"
    "        int inner(void) { return g + m; }\n"
    "        return inner();\n"
    "    };\n"
    "    return blk(3);\n"
    "}\n"
    "int snapshot_consistency(void) {\n"
    "    int g = 5;\n"
    "    int (^blk)(void) = ^{\n"
    "        int inner(void) { return g; }\n"
    "        return inner();\n"
    "    };\n"
    "    g = 100;\n"
    "    return blk();\n"
    "}\n"
    "int block_var_write(void) {\n"
    "    __block int g = 1;\n"
    "    int (^blk)(void) = ^{\n"
    "        int inner(void) { g += 3; return g; }\n"
    "        return inner();\n"
    "    };\n"
    "    int r1 = blk();\n"
    "    int r2 = g;\n"
    "    return (r1 == 4 && r2 == 4) ? 1 : 0;\n"
    "}\n"
    "int plain_write_stays_in_snapshot(void) {\n"
    "    int g = 5;\n"
    "    int (^blk)(void) = ^{\n"
    "        int inner(void) { g = 99; return g; }\n"
    "        return inner();\n"
    "    };\n"
    "    int r = blk();\n"
    "    return (r == 99 && g == 5) ? 1 : 0;\n"
    "}\n"
    "int main(void) {\n"
    "    if (ticket_repro() != 10) return 1;\n"
    "    if (snapshot_consistency() != 5) return 2;\n"
    "    if (!block_var_write()) return 3;\n"
    "    if (!plain_write_stays_in_snapshot()) return 4;\n"
    "    return 42;\n"
    "}\n"
)


def case_nested_fn_in_block_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  128: a nested function defined INSIDE a block, reading a "
          "variable owned by the block's own enclosing function, is a "
          "distinct VM miscompile from #1080's mirror nesting order -- "
          "broken on BOTH back-ends, not just -c=native (#1081). VM root "
          "cause: emit_static_chain_var_addr()'s static-link chase "
          "(codegen_addr.c) assumed every intermediate ancestor's own "
          "__static_link slot holds a plain frame bp; a block's own slot "
          "holds its descriptor pointer instead, so a chase that needs to "
          "hop THROUGH a block ancestor (depth >= 2 only) silently misread "
          "descriptor bytes as a frame pointer. Fixed by detecting the "
          "nearest block ancestor on the chase and terminating there, "
          "reading the variable out of that block's own capture "
          "descriptor -- which requires block_literal()'s transitive-"
          "capture climb (parse_blocks.c) to also walk every nested "
          "function defined directly inside a block's own body (Obj."
          "nested_children, parse_decl.c), so a variable referenced only "
          "inside such a nested function still ends up captured. Design "
          "decision: the nested function sees the block's OWN creation-"
          "time snapshot of an ancestor-owned variable (by-value, like a "
          "sibling direct block read already does), not a live read -- "
          "snapshot_consistency() pins this by mutating the ancestor "
          "variable between block creation and invocation. -c=native was "
          "independently broken too (compiled clean, segfaulted at "
          "runtime) -- its own nested-function-upvar machinery "
          "(NestedEnvEntry) was applied to a block ancestor as if it were "
          "a real nested function's env, chasing the block's real "
          "__static_link (its descriptor pointer) as another such env; "
          "fixed by stopping at the block and reading its real descriptor "
          "instead (block_ancestor_desc_ptr_expr(), serialize.c). Asserts "
          "VM 42 -> native 42.")
    return _vm_and_native_run_case(cccc, tmp, "nested_fn_in_block_1081",
                                    NESTED_FN_IN_BLOCK_1081_PROGRAM)


TYPEDEF_IDENTITY_1091_PROGRAM = (
    "typedef struct { int a, b; } Pair;\n"
    "typedef struct { int a, b; } Span;\n"
    "struct Tag { int v; };\n"
    "typedef struct { int v; } Tagless;\n"
    "typedef struct { long quot, rem; } ldiv_t;\n"
    "typedef struct { long long quot, rem; } lldiv_t;\n"
    "static Pair mk_pair(void) { Pair p; p.a = 1; p.b = 2; return p; }\n"
    "static Span mk_span(void) { Span s; s.a = 3; s.b = 4; return s; }\n"
    "static Tagless mk_tagless(void) { Tagless t; t.v = 5; return t; }\n"
    "int main(void) {\n"
    "    Pair p = mk_pair();\n"
    "    Span s = mk_span();\n"
    "    if (p.a + p.b != 3 || s.a + s.b != 7) return 1;\n"
    "    struct Tag tag; tag.v = 6;\n"
    "    Tagless t = mk_tagless();\n"
    "    if (tag.v != 6 || t.v != 5) return 2;\n"
    "    ldiv_t l = { .quot = -3, .rem = -2 };\n"
    "    lldiv_t ll = { .quot = -3, .rem = 2 };\n"
    "    if (l.quot != -3 || l.rem != -2 || ll.quot != -3 || ll.rem != 2) return 3;\n"
    "    int total = (p.a + p.b) + (s.a + s.b) + tag.v + t.v +\n"
    "                (int)(l.quot + l.rem) + (int)(ll.quot + ll.rem);\n"
    "    return total == 15 ? 42 : 4;\n"
    "}\n"
)


def case_typedef_identity_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  129: -c=native's serializer collapsed structurally-identical-"
          "but-nominally-distinct typedef'd structs into one printed type "
          "-- same_type_or_origin()'s structural fallback (load-bearing for "
          "#1006/#1046's own same-declaration-reparsed dedup) meant two "
          "UNRELATED same-shaped typedefs (e.g. ldiv_t/lldiv_t, byte-"
          "identical on every 64-bit target this project supports) "
          "collapsed into one spelling, and a tagless typedef next to a "
          "same-shaped TAGGED struct got spelled with the tagged struct's "
          "own name (#1091). Found verifying #1090's div/ldiv/lldiv fix. "
          "Fixed with identity-before-structure in find_typedef_name() "
          "(find_typedef_name_exact()'s existing ->origin-chain walk, tried "
          "first), a tag_spelling_mismatch() guard in find_tag_name()/"
          "type_has_tag_for_owner() (a tagless type is never spelled with a "
          "same-shaped tagged one's tag), and nominal-aware ctx->defs/"
          "ctx->emitted_defs dedup (type_vec_push_nominal(), gated on two "
          "structurally-equal Type objects each resolving via identity to "
          "a DIFFERENTLY-named typedef record) so each nominally-distinct "
          "type gets its own printed definition. Asserts -m output prints "
          "standalone bodies for both Pair and Span (not one collapsed "
          "onto the other's name), Tagless as its own tagless body (not "
          "spelled `struct Tag`), plus VM 42 -> native 42 -- this is the "
          "round-trip proof: a pre-fix build fails outright under "
          "-c=native (redefinition, or \"assigning to X from incompatible "
          "type Y\"), not just wrong text.")
    src = Path(tmp) / "typedef_identity_1091.c"
    write(src, TYPEDEF_IDENTITY_1091_PROGRAM)

    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    out = m_result.stdout
    if "} Pair;" not in out or "} Span;" not in out:
        print(f"    FAIL: -m output missing standalone Pair/Span bodies "
              f"(nominally-distinct tagless typedefs collapsed)\n    {out}")
        return False
    if "struct Tag A" in out or "struct Tag Tagless" in out:
        print(f"    FAIL: -m output spells Tagless using struct Tag's own "
              f"tag\n    {out}")
        return False

    return _vm_and_native_run_case(cccc, tmp, "typedef_identity_1091",
                                    TYPEDEF_IDENTITY_1091_PROGRAM)


THREADS_NATIVE_PROGRAM = (
    # Exercises every one of the 24 thrd_*/mtx_*/cnd_*/tss_*/call_once shim
    # functions at least once -- each is emitted as raw fprintf'd C text,
    # gated per-function on whether the program actually calls it, so a
    # program touching only a handful (as an earlier version of this case
    # did) leaves the rest never compiled by any host compiler at all.
    # mtx_t/cnd_t are file-scope `static` (zero-initialized by C's own
    # static-storage-duration rule), matching every other <threads.h> test
    # in this repo -- the shims' lazy ->__handle allocation reads that
    # field before mtx_init/cnd_init would otherwise touch it, so an
    # uninitialized *automatic* mtx_t/cnd_t is not a supported starting
    # point here (the same assumption src/stdlib/pthread.c's own
    # ensure_mtx/ensure_cond already make on the VM side).
    "#include <threads.h>\n"
    "#include <stdatomic.h>\n"
    "#include <time.h>\n"
    "static mtx_t mutex;\n"
    "static cnd_t cond;\n"
    "static int ready = 0;\n"
    "static int shared_val = 0;\n"
    "static _Atomic int woke_count = 0;\n"
    "static _Atomic int detached_ran = 0;\n"
    "static once_flag once = ONCE_FLAG_INIT;\n"
    "static int once_calls = 0;\n"
    "static void init_once(void) { once_calls++; }\n"
    "static int waiter(void *arg) {\n"
    "    (void)arg;\n"
    "    mtx_lock(&mutex);\n"
    "    while (!ready)\n"
    "        cnd_wait(&cond, &mutex);\n"
    "    atomic_fetch_add(&woke_count, 1);\n"
    "    mtx_unlock(&mutex);\n"
    "    return 0;\n"
    "}\n"
    "static int broadcaster(void *arg) {\n"
    "    (void)arg;\n"
    "    call_once(&once, init_once);\n"
    "    mtx_lock(&mutex);\n"
    "    shared_val = 1;\n"
    "    ready = 1;\n"
    "    cnd_broadcast(&cond);\n"
    "    mtx_unlock(&mutex);\n"
    "    return 0;\n"
    "}\n"
    "static int detached_worker(void *arg) {\n"
    "    (void)arg;\n"
    "    atomic_store(&detached_ran, 1);\n"
    "    thrd_exit(7);\n"
    "    return 99;\n"
    "}\n"
    "int main(void) {\n"
    "    if (mtx_init(&mutex, mtx_plain) != thrd_success) return 1;\n"
    "    if (cnd_init(&cond) != thrd_success) return 2;\n"
    "\n"
    "    thrd_t w1, w2, b;\n"
    "    if (thrd_create(&w1, waiter, 0) != thrd_success) return 3;\n"
    "    if (thrd_create(&w2, waiter, 0) != thrd_success) return 4;\n"
    "    struct timespec nap = {0, 20000000};\n"
    "    thrd_sleep(&nap, 0);\n"
    "    if (thrd_create(&b, broadcaster, 0) != thrd_success) return 5;\n"
    "    thrd_join(w1, 0);\n"
    "    thrd_join(w2, 0);\n"
    "    thrd_join(b, 0);\n"
    "    if (shared_val != 1 || atomic_load(&woke_count) != 2) return 6;\n"
    "    if (once_calls != 1) return 7;\n"
    "\n"
    "    if (mtx_trylock(&mutex) != thrd_success) return 8;\n"
    "    mtx_unlock(&mutex);\n"
    "\n"
    "    struct timespec ts;\n"
    "    timespec_get(&ts, TIME_UTC);\n"
    "    ts.tv_sec += 1;\n"
    "    if (mtx_timedlock(&mutex, &ts) != thrd_success) return 9;\n"
    "    mtx_unlock(&mutex);\n"
    "\n"
    "    struct timespec deadline;\n"
    "    timespec_get(&deadline, TIME_UTC);\n"
    "    deadline.tv_nsec += 20000000;\n"
    "    if (deadline.tv_nsec >= 1000000000) {\n"
    "        deadline.tv_sec += 1;\n"
    "        deadline.tv_nsec -= 1000000000;\n"
    "    }\n"
    "    mtx_lock(&mutex);\n"
    "    int rc = cnd_timedwait(&cond, &mutex, &deadline);\n"
    "    mtx_unlock(&mutex);\n"
    "    if (rc != thrd_timedout) return 10;\n"
    "\n"
    "    thrd_t self = thrd_current();\n"
    "    if (!thrd_equal(self, thrd_current())) return 11;\n"
    "    if (thrd_equal(w1, w2)) return 12;\n"
    "\n"
    "    thrd_t t3;\n"
    "    if (thrd_create(&t3, detached_worker, 0) != thrd_success) return 13;\n"
    "    if (thrd_detach(t3) != thrd_success) return 14;\n"
    "    for (int i = 0; i < 1000 && !atomic_load(&detached_ran); i++) {\n"
    "        struct timespec poll_nap = {0, 1000000};\n"
    "        thrd_sleep(&poll_nap, 0);\n"
    "    }\n"
    "    if (!atomic_load(&detached_ran)) return 15;\n"
    "\n"
    "    cnd_destroy(&cond);\n"
    "    mtx_destroy(&mutex);\n"
    "    return 42;\n"
    "}\n"
)


def case_threads_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  130: no real <threads.h> lowering existed for -c=native at all"
          " -- thrd_create/mtx_lock/etc. (include/threads.h) are VM cfuncs "
          "(src/stdlib/pthread.c) with no host libc symbol to link against, "
          "so a native binary calling one failed at the linker with no "
          "CCCC-side diagnostic (#1088). threads.h is already on "
          "is_cccc_supplied_only_header() (preprocess.c), so its types "
          "(mtx_t/cnd_t/thrd_t/tss_t) already re-derived correctly -- only "
          "the function *definitions* were missing. Fixed with a "
          "self-contained shim (serialize_threads_shims, src/serialize.c) "
          "defining thrd_*/mtx_*/cnd_*/tss_*/call_once directly over the "
          "already-replayed real host <pthread.h>, rather than a "
          "#include_next hand-off onto a real host <threads.h> the way "
          "include/pthread.h itself hands off (#1022) -- CCCC's own "
          "thrd_error/thrd_timedout/thrd_busy/thrd_nomem encoding does not "
          "match glibc's, and Darwin has no <threads.h> at all, so a "
          "hand-off would leave macOS permanently unsupported; a "
          "self-contained shim closes both platforms in one change, "
          "consulting the host's own <threads.h> on neither. call_once "
          "also stopped being a guest-side macro (a plain, non-atomic flag "
          "check, safe only under the VM's own GIL) and became a real "
          "function, backed by an atomic CAS on both backends. Deliberately "
          "passes an explicit -I<repo>/include, like case 118 -- without it "
          "the replayed #include <pthread.h> wouldn't resolve through "
          "CCCC's own bundled header at all. Asserts -m output carries a "
          "real 'int thrd_create(' *definition* (not merely a prototype -- "
          "distinguished by checking no bare 'int thrd_create(thrd_t *thr, "
          "thrd_start_t func, void *arg);' prototype-only line follows "
          "immediately after a definition's closing brace on the same "
          "construct), plus VM 42 -> native 42 with two threads racing a "
          "mutex-protected counter and a call_once initializer.")
    src = Path(tmp) / "threads_native_1088.c"
    write(src, THREADS_NATIVE_PROGRAM)
    include_dir = cccc.parent / "include"

    vm_result = run([str(cccc), "-I", str(include_dir), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False

    m_result = run([str(cccc), "-I", str(include_dir), "-m", src.name], cwd=tmp)
    out = m_result.stdout
    if "int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {" not in out:
        print(f"    FAIL: -m output missing a real thrd_create definition\n    {out}")
        return False
    # #1183/#1184: both the parameter type and the function name itself
    # spell their private forms -- see include/threads.h's own comment on
    # the rename (once_flag/call_once are guest-side-only #define aliases,
    # to avoid colliding with a real glibc declaration of either name,
    # pulled in unconditionally by this shim's own `#include <stdlib.h>`
    # under any C11/GNU dialect -- once_flag's own typedef, and on a new
    # enough glibc, a real ISO C11 call_once declaration too).
    if "void __cccc_call_once(__cccc_once_flag *flag, void (*func)(void)) {" not in out:
        print(f"    FAIL: -m output missing a real call_once definition\n    {out}")
        return False
    # Regression guard for #1183/#1184: neither alias's #define may reach
    # native output at all (they'd stay live for the rest of the
    # generated translation unit, corrupting a later-replayed real host
    # declaration of the same name), and the bare host-visible spellings
    # must never appear as a *typedef target*/*function definition* ahead
    # of the shim's own #include <stdlib.h> -- that's the literal shape of
    # both bugs (a plain `typedef ... once_flag;`/`... call_once(...) {`
    # colliding with glibc's own declaration of the same name).
    if "#define once_flag" in out or "#define call_once" in out:
        print(f"    FAIL: -m output replays the once_flag/call_once "
              f"private-name alias #define (#1184)\n    {out}")
        return False
    if "typedef int once_flag;" in out or "typedef _Atomic int once_flag;" in out:
        print(f"    FAIL: -m output re-materializes a host-colliding "
              f"'once_flag' typedef (#1183)\n    {out}")
        return False
    if "void call_once(" in out:
        print(f"    FAIL: -m output re-materializes a host-colliding "
              f"'call_once' definition (#1184)\n    {out}")
        return False

    out_bin = Path(tmp) / "threads_native_1088_out"
    compile_result = run(
        [str(cccc), "-I", str(include_dir), "-c=native", "-o", out_bin.name,
         src.name],
        cwd=tmp,
    )
    if compile_result.returncode != 0:
        print(f"    FAIL: native compile exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out_bin.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n    {run_result.stderr}")
        return False

    print("    ok")
    return True


EMOJI_MACRO_PROGRAM = (
    "#define \U0001FAB1 - ~\n"
    "#define \U0001F40D ~-\n"
    "#define ascii_worm - ~\n"
    "#ifndef ascii_worm\n"
    "#error \"ASCII-named define was lost\"\n"
    "#endif\n"
    "int main(void) {\n"
    "    if ((\U0001FAB1 42) != 43) return 1;\n"   # -~42
    "    if ((\U0001F40D 42) != 41) return 2;\n"   # ~-42
    "    if ((ascii_worm 40) != 41) return 3;\n"
    "#undef \U0001FAB1\n"
    "#undef \U0001F40D\n"
    "#ifdef \U0001FAB1\n"
    "#error \"emoji #undef did not take effect\"\n"
    "#endif\n"
    "    if (-~41 != 42) return 4;\n"
    "    return 42;\n"
    "}\n"
)


def case_native_emoji_macro_define_not_replayed(cccc: Path, tmp: str) -> bool:
    print("  131: auto-captured #define/#undef lines whose macro NAME is "
          "non-ASCII (emoji identifiers -- an accepted CCCC extension, e.g. "
          "test_suite_misc.c's worm/snake operator macros) used to replay "
          "verbatim into -m/-c=native output, where the host preprocessor "
          "rejects the name outright ('macro name must be an identifier', xN "
          "for defines plus matching #undefs), failing an otherwise-clean "
          "native compile even though every in-AST use was already expanded "
          "at parse time (#1118). Fixed by dropping such lines from "
          "cc_serialize_program()'s emit_directives replay loop "
          "(line_macro_name_is_non_ascii, src/serialize.c), gated off under "
          "--emit-cccc like every other filter in that loop; ASCII-named "
          "defines still replay. Asserts -m output contains no non-ASCII-"
          "named define/undef line (while ASCII ones still replay), and "
          "VM 42 -> native 42 for a program defining/using/undefining emoji "
          "macros at file scope.")
    src = Path(tmp) / "emoji_macro_1118.c"
    write(src, EMOJI_MACRO_PROGRAM)
    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    non_ascii_replayed = [
        line for line in m_result.stdout.splitlines()
        if any(ord(ch) >= 0x80 for ch in line)
        and ("define" in line or "undef" in line)
    ]
    if non_ascii_replayed:
        print(f"    FAIL: -m output still replays a non-ASCII-macro "
              f"directive line: {non_ascii_replayed}")
        return False
    if not any(line.strip().startswith("#define ascii_worm")
               for line in m_result.stdout.splitlines()):
        print(f"    FAIL: -m output stopped replaying ASCII-named defines\n"
              f"    {m_result.stdout}")
        return False
    return _vm_and_native_run_case(cccc, tmp, "emoji_macro_1118",
                                    EMOJI_MACRO_PROGRAM)


ATOMIC_FETCH_PROGRAM = (
    "#include <stdatomic.h>\n"
    "int main(void) {\n"
    "    _Atomic int x = 10;\n"
    "    int old = atomic_fetch_add(&x, 5);\n"
    "    if (old != 10 || atomic_load(&x) != 15) return 1;\n"
    "    old = atomic_fetch_sub(&x, 3);\n"
    "    if (old != 15 || atomic_load(&x) != 12) return 2;\n"
    "    return 42;\n"
    "}\n"
)


def case_atomic_fetch_cas_loop_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  137: atomic_fetch_add/sub/or/xor/and (stdatomic.h) used to "
          "expand to a plain, non-atomic load-then-store -- correct only "
          "under the VM's GIL (never released between bytecode "
          "instructions), a genuine data race with silently lost updates "
          "under -c=native's real thread parallelism (#1184). Fixed by "
          "rewriting the macros as a CAS retry loop, the same shape "
          "to_assign() already builds for `_Atomic x += y`. Asserts -m "
          "output lowers atomic_fetch_add/sub to a real "
          "__atomic_compare_exchange_n retry loop rather than a bare "
          "__atomic_load_n/__atomic_store_n pair, and VM 42 -> native 42.")
    src = Path(tmp) / "atomic_fetch_cas_1184.c"
    write(src, ATOMIC_FETCH_PROGRAM)
    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "__atomic_compare_exchange_n" not in m_result.stdout:
        print(f"    FAIL: -m output does not lower atomic_fetch_* to a CAS "
              f"loop\n    {m_result.stdout}")
        return False
    if "__atomic_store_n" in m_result.stdout:
        print(f"    FAIL: -m output still contains a bare __atomic_store_n "
              f"-- atomic_fetch_* regressed to load-then-store\n"
              f"    {m_result.stdout}")
        return False
    return _vm_and_native_run_case(cccc, tmp, "atomic_fetch_cas_1184",
                                    ATOMIC_FETCH_PROGRAM)


ATOMIC_FENCE_PROGRAM = (
    "#include <stdatomic.h>\n"
    "int main(void) {\n"
    "    _Atomic int x = 0;\n"
    "    atomic_thread_fence(memory_order_seq_cst);\n"
    "    int order = memory_order_acquire;\n"
    "    atomic_signal_fence(order);\n"
    "    atomic_store(&x, 7);\n"
    "    if (atomic_load(&x) != 7) return 1;\n"
    "    return 42;\n"
    "}\n"
)


def case_atomic_fence_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  138: atomic_thread_fence/atomic_signal_fence (stdatomic.h) "
          "used to expand to nothing -- harmless under the VM's GIL, but a "
          "real reordering hazard under -c=native's genuine thread "
          "parallelism (#1188, found while fixing #1184). Fixed by lowering "
          "to a real __atomic_thread_fence/__atomic_signal_fence via a new "
          "ND_FENCE node. Asserts -m output emits the symbolic "
          "__ATOMIC_SEQ_CST for a constant order and passes a non-constant "
          "order through verbatim rather than dropping it, and VM 42 -> "
          "native 42.")
    src = Path(tmp) / "atomic_fence_1188.c"
    write(src, ATOMIC_FENCE_PROGRAM)
    m_result = run([str(cccc), "-m", src.name], cwd=tmp)
    if "__atomic_thread_fence(__ATOMIC_SEQ_CST)" not in m_result.stdout:
        print(f"    FAIL: -m output does not emit a constant-folded "
              f"__atomic_thread_fence(__ATOMIC_SEQ_CST)\n    {m_result.stdout}")
        return False
    if "__atomic_signal_fence(order)" not in m_result.stdout:
        print(f"    FAIL: -m output does not pass a non-constant order "
              f"through to __atomic_signal_fence verbatim\n"
              f"    {m_result.stdout}")
        return False
    if "atomic_thread_fence(" in m_result.stdout.replace(
            "__atomic_thread_fence(", ""):
        print(f"    FAIL: -m output still contains an unlowered "
              f"atomic_thread_fence call\n    {m_result.stdout}")
        return False
    return _vm_and_native_run_case(cccc, tmp, "atomic_fence_1188",
                                    ATOMIC_FENCE_PROGRAM)


ATOMIC_VAR_INIT_PROGRAM = (
    "#include <stdatomic.h>\n"
    "int main(void) {\n"
    "    atomic_int x = ATOMIC_VAR_INIT(5);\n"
    "    if (atomic_load(&x) != 5) return 1;\n"
    "    return 42;\n"
    "}\n"
)


def case_atomic_var_init_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  139: ATOMIC_VAR_INIT was missing from include/stdatomic.h "
          "entirely -- a C11/C17 program using it (C11 7.17.2p1, deprecated "
          "in C17, removed in C23) got an undefined-macro error instead of "
          "the working expansion a real C11/C17 compiler gives (#1190). "
          "Fixed with a `(value)` expansion gated to "
          "__STDC_VERSION__ <= 201710L, matching how glibc/clang gate it -- "
          "left undefined under cccc's default C23, same as a real C23 "
          "compiler. Asserts --std=c11 VM 42 -> --std=c11 native 42.")
    src = Path(tmp) / "atomic_var_init_1190.c"
    write(src, ATOMIC_VAR_INIT_PROGRAM)
    vm_result = run([str(cccc), "--std=c11", src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: --std=c11 VM exit {vm_result.returncode}\n"
              f"    {vm_result.stderr}")
        return False
    out = Path(tmp) / "atomic_var_init_1190_out"
    compile_result = run(
        [str(cccc), "--std=c11", "-c=native", "-o", out.name, src.name],
        cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: --std=c11 native compile exited "
              f"{compile_result.returncode}\n    {compile_result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: --std=c11 native exit {run_result.returncode}\n"
              f"    {run_result.stderr}")
        return False
    print("    ok")
    return True


FTS_OPAQUE_HANDLE_PROGRAM = (
    "#include <fts.h>\n"
    "#include <stddef.h>\n"
    "int main(void) {\n"
    "    char *paths[2] = {\".\", NULL};\n"
    "    FTS *fts = fts_open(paths, 16 | 4, NULL);\n"
    "    if (!fts) return 1;\n"
    "    FTSENT *e;\n"
    "    while ((e = fts_read(fts)) != NULL) { (void)e; }\n"
    "    fts_close(fts);\n"
    "    return 42;\n"
    "}\n"
)


def case_opaque_handle_native_round_trip(cccc: Path, tmp: str) -> bool:
    print("  140: FTS (include/fts.h, and the same shape for DIR/DBM) is a "
          "deliberately never-completed opaque handle typedef -- "
          "`typedef struct __cccc_FTS FTS;`. serialize_type() used to "
          "spell it by its tag at declaration sites (`struct __cccc_FTS "
          "*fts;`) but by its alias at #1107's from_include cast sites "
          "(`(FTS *)fts_open(...)`) -- the two disagree once the replayed "
          "#include hands the real host FTS to the compiled output (the "
          "#1143 -idirafter demotion), 'assignment to struct __cccc_FTS * "
          "from incompatible pointer type FTS *' on a host compiler that "
          "promotes -Wincompatible-pointer-types to an error (GCC 14+; not "
          "clang, and not the older GCC in local container verification -- "
          "which is why sr.ht's own build hardware caught this first, "
          "#1186). Fixed by preferring the alias whenever the tag is an "
          "opaque, never-completed struct with a from_include typedef "
          "(serialize_type.c's TY_STRUCT case), so every site -- "
          "declaration, argument, assignment -- agrees. Asserts -m output "
          "never spells the internal tag at all (only the real FTS alias), "
          "plus VM 42 -> native 42; tests/test_fts_standalone.c (a fuller, "
          "real-filesystem exercise of the same fts_open/fts_read/"
          "fts_close chain) is the runtime-behavior half of this "
          "regression, wired into the ordinary --native corpus.")
    # fts.h transitively #include "../time.h"s off sys/stat.h -- a relative
    # quoted include from inside an embedded header, which cccc's own
    # preprocessor now resolves against the embedded table regardless of
    # tmp's working directory (#1194). The explicit -I<repo>/include here is
    # for a different, still-real reason: -c=native's replayed #include
    # lines are read by the real HOST compiler, which needs the bundled
    # headers on an actual search path on disk (same pattern several cases
    # above use, e.g. #1088's threads.h case) -- unrelated to cccc's own
    # resolution of the file it's compiling.
    include_dir = cccc.parent / "include"
    src = Path(tmp) / "opaque_handle_1186.c"
    write(src, FTS_OPAQUE_HANDLE_PROGRAM)
    m_result = run([str(cccc), "-I", str(include_dir), "-m", src.name], cwd=tmp)
    if "struct __cccc_" in m_result.stdout:
        print(f"    FAIL: -m output still spells the internal opaque tag "
              f"(struct __cccc_...) rather than the real FTS alias\n"
              f"    {m_result.stdout}")
        return False
    if "FTS *fts" not in m_result.stdout and "FTS*fts" not in m_result.stdout:
        print(f"    FAIL: -m output does not declare `fts` at the real FTS "
              f"alias type\n    {m_result.stdout}")
        return False
    vm_result = run([str(cccc), "-I", str(include_dir), src.name], cwd=tmp)
    if vm_result.returncode != 42:
        print(f"    FAIL: VM exit {vm_result.returncode}\n    {vm_result.stderr}")
        return False
    out = Path(tmp) / "opaque_handle_1186_out"
    compile_result = run(
        [str(cccc), "-I", str(include_dir), "-c=native", "-o", out.name, src.name],
        cwd=tmp)
    if compile_result.returncode != 0:
        print(f"    FAIL: native compile exited {compile_result.returncode}\n"
              f"    {compile_result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: native exit {run_result.returncode}\n"
              f"    {run_result.stderr}")
        return False
    print("    ok")
    return True


# Every case this script runs, in a fixed order matching each case's own
# hand-maintained case number (see each function's own print()). Hoisted to
# module scope (#1197) so both main() and audit_skips() below share one
# source of truth -- previously this was a local inside main()'s
# `with tempfile.TemporaryDirectory()` block.
CASES = [
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
    case_testing_bytecode_prepass_compiles,
    case_testing_native_prepass_compiles,
    case_testing_failing_suite_refuses_compile,
    case_testing_build_blocked_by_failing_suite,
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
    case_double_literal_native_round_trip,
    case_va_list_size_native_round_trip,
    case_va_list_translation_native_round_trip,
    case_va_arg_promotion_native_round_trip,
    case_stdarg_guard_native_round_trip,
    case_issignaling_native_round_trip,
    case_native_std_ladder,
    case_native_defines_survive_argv,
    case_native_signed_char_argv,
    case_native_cond_directive_not_replayed,
    case_flt_rounds_native_round_trip,
    case_native_explicit_std_probed,
    case_nested_decl_binding_native_round_trip,
    case_mb_cur_max_native_round_trip,
    case_nested_fn_native_round_trip,
    case_struct_byval_param_copy,
    case_nested_fn_shadow_native_round_trip,
    case_f2i_native_round_trip,
    case_ctor_dtor_native_round_trip,
    case_attribute_survives_after_stdio_include,
    case_va_list_param_native_round_trip,
    case_va_list_libc_call_native_round_trip,
    case_pthread_native_round_trip,
    case_byval_member_order_native_round_trip,
    case_offsetof_array_len_native_round_trip,
    case_static_libc_collision_native_round_trip,
    case_layout_const_native_round_trip,
    case_static_label_table_native_round_trip,
    case_bundled_header_bodiless_decl_no_include_dir,
    case_layout_const_sites_native_round_trip,
    case_static_assert_native_round_trip,
    case_sizeof_only_aggregate_native_round_trip,
    case_scalar_member_not_host_owned_native_round_trip,
    case_scalar_host_divergent_native_round_trip,
    case_block_in_nested_native_round_trip,
    case_nested_fn_in_block_native_round_trip,
    case_typedef_identity_native_round_trip,
    case_threads_native_round_trip,
    case_native_emoji_macro_define_not_replayed,
    case_atomic_fetch_cas_loop_native_round_trip,
    case_atomic_fence_native_round_trip,
    case_atomic_var_init_native_round_trip,
    case_opaque_handle_native_round_trip,
]


def main() -> int:
    root = Path(__file__).parent.parent.resolve()
    cccc = root / "cccc"

    print("Native-backend serializer smoke tests (#892/#897/#901/#904/#918/#925/#926/#927/#928/#952/#953/#956/#963/#964/#968/#971/#973/#976/#977/#982/#965/#989/#990/#993/#996/#995/#998/#999/#1002/#1003/#1005/#1006/#1010/#1011/#1014/#1015/#1016/#967/#1031/#1019/#1042/#1034/#1046/#1051/#1045/#1049/#1047/#1050/#1048/#1057/#1054/#1030/#1058/#1059/#1018/#1063/#1064/#1071/#1056/#1069/#1074/#1078/#1075/#1068/#1020/#1083/#1062/#1085/#1022/#1044/#1096/#1095/#1098/#1080/#1081/#1091/#1088/#1118/#1184/#1188/#1190/#1186)")

    if not cccc.exists():
        print(f"  FAIL: {cccc.name} not found — run 'make' first.")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        cases = CASES
        results = []
        for case in cases:
            reason = smoke_case_skip_reason(case.__name__)
            if reason:
                print(f"  SKIP {case.__name__}: {reason}")
                results.append(None)
                continue
            results.append(case(cccc, tmp))

    failed = results.count(False)
    skipped = results.count(None)
    passed = len(results) - failed - skipped
    if failed:
        print(f"{failed} of {len(results)} native-backend serializer smoke cases FAILED"
              f"{f' ({skipped} skipped)' if skipped else ''}.")
        return 1
    if skipped:
        print(f"{passed} passed, {skipped} skipped "
              f"(of {len(results)} native-backend serializer smoke cases).")
        return 0
    print(f"All {len(results)} native-backend serializer smoke cases passed.")
    return 0


def audit_skips() -> int:
    """Behavioural staleness audit for SMOKE_CASE_SKIPS_GCC_MACOS (#1197),
    mirroring _print_native_skip_audit()'s STALE/KEPT contract
    (tools/testing/cli.py) for the NATIVE_SKIP_TESTS* filename-keyed tables.

    Unlike a plain CCCC_AUDIT_NATIVE_SKIPS=1 run of main() -- which just
    un-skips every applicable case and inverts pass/fail into the ordinary
    "N passed"/"FAILED" report -- this classifies each case actually
    governed by an entry on this platform+family as STALE (it now passes
    with the table bypassed; delete the entry) or justified (still fails;
    the entry is earning its keep), and exits nonzero only for the former.
    That's the verdict tools/run_tests.py's smoke_skip_audit sub-suite
    needs to hard-fail on real staleness the way native_skip_audit already
    does for the other tables.

    Restricts itself to cases smoke_entry_applies_here() says are actually
    governed here -- on any platform/family other than macOS+gcc that's
    every case, so this reports "nothing to audit" rather than a false
    all-clear.
    """
    root = Path(__file__).parent.parent.resolve()
    cccc = root / "cccc"
    if not cccc.exists():
        print(f"  FAIL: {cccc.name} not found — run 'make' first.")
        return 1

    platform = detect_platform()
    family = detect_native_cc_family()
    audited = [c for c in CASES if smoke_entry_applies_here(c.__name__, platform, family)]

    print()
    print("=== comptime_native_smoke.py --audit-skips report (#1197) ===")
    if not audited:
        print(f"nothing to audit on this platform/family ({platform}/{family}) -- "
              f"SMOKE_CASE_SKIPS_GCC_MACOS only applies on macos/gcc")
        return 0

    os.environ["CCCC_AUDIT_NATIVE_SKIPS"] = "1"
    stale, justified = [], []
    with tempfile.TemporaryDirectory() as tmp:
        for case in audited:
            ok = case(cccc, tmp)
            (stale if ok else justified).append(case.__name__)

    if stale:
        print(f"STALE ({len(stale)}) -- now pass; delete the skip entry:")
        for name in stale:
            print(f"  {name}")
    if justified:
        print(f"KEPT, still fails with the table bypassed ({len(justified)}) -- "
              f"entry is not stale:")
        for name in justified:
            print(f"  {name}")
    print("=" * 44)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(audit_skips() if "--audit-skips" in sys.argv else main())
