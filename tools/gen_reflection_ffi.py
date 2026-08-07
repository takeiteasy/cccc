#!/usr/bin/env python3
"""Generate macros.c's/reflection.c's comptime-builtin FFI block from
include/cccc/reflection.h.

reflection.h is the canonical signature for every comptime builtin -- it's
injected verbatim into user comptime code, and is already parsed by
`-c=generated`/tools/generate_stdlib.c for that purpose. Before this generator, src/macros.c
hand-duplicated every one of its ~157 prototypes as a matching `extern`
(so register_reflection_ffi() could take function addresses) and hand-typed
the cc_register_cfunc()/cc_register_variadic_cfunc() arity for each one --
three places that had to stay in sync by hand, with no static check tying
them together (src/reflection.c, which holds the actual definitions, doesn't
even include reflection.h). Two real drift bugs (#854) came from exactly
this: builtins declared + implemented but never registered, and a
registration arity one short of the real parameter count.

This script parses every file-scope `RET __builtin_NAME(params);` prototype
out of reflection.h (in header order) and emits two generated, *committed*
files:

  src/reflection_ffi_protos.inc    -- one `extern` per builtin, copied
                                       verbatim from the header's return
                                       type/params/attribute.
  src/reflection_ffi_register.inc  -- the register_reflection_ffi() body:
                                       one cc_register_cfunc()/
                                       cc_register_variadic_cfunc() call per
                                       builtin, with a generated arity.

Both are #include'd by src/macros.c *and* src/reflection.c, so any drift
between reflection.h's prototype and reflection.c's definition is now a
compile error, not just an unregistered/mismatched-arity builtin.

These files are committed (like src/std_stub.c, unlike the gitignored
src/std.c) so that plain `make` -- no python3 required -- still satisfies
the stage0 bootstrap invariant documented in the Makefile. `./cccc --build
build.c`'s default build regenerates them via the reflection-ffi-gen step;
`--check` (wired into the `test` build target / tools/run_tests.py) fails
the build if the committed files are stale.

Usage:
    python3 tools/gen_reflection_ffi.py          # write/refresh the .inc files
    python3 tools/gen_reflection_ffi.py --check  # verify they're up to date
"""
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
REFLECTION_H = REPO_ROOT / "include" / "cccc" / "reflection.h"
PROTOS_INC = REPO_ROOT / "src" / "reflection_ffi_protos.inc"
REGISTER_INC = REPO_ROOT / "src" / "reflection_ffi_register.inc"

BANNER = """\
/* GENERATED FILE -- DO NOT EDIT.
 * Produced by tools/gen_reflection_ffi.py from include/cccc/reflection.h.
 * Regenerate with: python3 tools/gen_reflection_ffi.py
 */
"""

# Matches a file-scope prototype `RET __builtin_NAME(params) [attr];`,
# tolerating the header's occasional two-line wrap (params spilling onto a
# continuation line, or a trailing __attribute__((...)) on its own line).
# `params` allows one level of balanced parens (a function-pointer-typed
# parameter, e.g. `void (*cb)(int)`) even though reflection.h has none
# today -- cheap to support now so a future one is parsed, not silently
# dropped past the completeness guard below (which shares this same
# limitation for anything it can't balance-match at all).
_PARAM_CHARS = r'(?:[^;{)(]|\([^()]*\))*'
PROTO_RE = re.compile(
    r'([A-Za-z_][\w \t\*]*?)\b(__builtin_\w+)\s*\((' + _PARAM_CHARS + r')\)\s*'
    r'((?:__attribute__\s*\([^;]*\))?)\s*;'
)


def strip_comments(text):
    """Blank out comment bodies while preserving line numbers/newlines, so
    regex match positions still map back to real source lines for error
    messages."""
    def blank_block(m):
        return re.sub(r'[^\n]', ' ', m.group(0))
    text = re.sub(r'/\*.*?\*/', blank_block, text, flags=re.S)
    text = re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), text)
    return text


def param_list_arity(params):
    """Returns (fixed_arg_count, is_variadic) for a raw parameter-list
    string (already comment-stripped, not yet split)."""
    params = params.strip()
    if params == '' or params == 'void':
        return 0, False
    parts = [p.strip() for p in params.split(',')]
    variadic = parts[-1] == '...'
    if variadic:
        parts = parts[:-1]
    return len(parts), variadic


def return_kind(ret_type):
    """Tri-state return encoding used by cc_register_cfunc/_variadic_cfunc
    and cc_dlsym (src/vm.c: set_return_kind/get_return_kind):
    0 = int/pointer-sized, 1 = double, 2 = float."""
    ret_type = ret_type.strip()
    if ret_type == 'double':
        return 1
    if ret_type == 'float':
        return 2
    return 0


def parse_prototypes(text):
    """Returns (list of (ret, name, params, attr) in header order, set of
    __builtin_ names that look declaration-shaped but weren't matched)."""
    stripped = strip_comments(text)
    protos = []
    seen_names = set()
    for m in PROTO_RE.finditer(stripped):
        ret, name, params, attr = m.groups()
        if name in seen_names:
            continue  # shouldn't happen in reflection.h; first wins if it does
        seen_names.add(name)
        protos.append((ret.strip(), name, params.strip(), attr.strip()))

    # Completeness guard: any other column-0 `RET __builtin_NAME(params)...`
    # signature that PROTO_RE failed to capture means the regex missed a
    # real prototype -- fail loudly rather than silently regenerating a
    # stale/incomplete FFI table. Anchoring to column 0 (reflection.h's
    # consistent style for both prototypes and body-defined builtins) is
    # what lets this skip, without special-casing them by name:
    #   - indented call-usage sites inside another function/macro body
    #     (e.g. the `__builtin_generate_getters(ty);` call inside
    #     __builtin_attr_generate_getters)
    #   - #define lines (e.g. __builtin_dispatch_2/_3), which never start
    #     with a return-type token
    # A column-0 signature ending in '{' (e.g. __builtin_generate_getters
    # itself, or the @comptime attribute handlers) is a real function
    # defined with a body directly in the header -- compiled into every
    # macro program, not FFI-registered -- and is deliberately treated as
    # handled, not flagged.
    COL0_SIG_RE = re.compile(
        r'^([A-Za-z_][\w \t\*]*?)\b(__builtin_\w+)\s*\((' + _PARAM_CHARS + r')\)\s*'
        r'((?:__attribute__\s*\([^;{]*\))?)\s*([;{])',
        re.MULTILINE,
    )
    unmatched = set()
    for m in COL0_SIG_RE.finditer(stripped):
        name, terminator = m.group(2), m.group(5)
        if terminator == '{':
            continue  # defined with a body right here -- not FFI-registered
        if name in seen_names:
            continue
        unmatched.add(name)
    return protos, unmatched


def render_protos_inc(protos):
    lines = [BANNER, ""]
    for ret, name, params, attr in protos:
        params_out = params if params else "void"
        line = f"extern {ret} {name}({params_out})"
        if attr:
            line += f"\n    {attr}"
        line += ";"
        lines.append(line)
    lines.append("")
    return "\n".join(lines)


def render_register_inc(protos):
    lines = [BANNER, ""]
    for ret, name, params, attr in protos:
        n, variadic = param_list_arity(params)
        rd = return_kind(ret)
        if variadic:
            lines.append(
                f'cc_register_variadic_cfunc(vm, "{name}", (void *){name}, {n}, {rd});'
            )
        else:
            lines.append(
                f'cc_register_cfunc(vm, "{name}", (void *){name}, {n}, {rd});'
            )
    lines.append("")
    return "\n".join(lines)


def write_if_changed(path, content):
    if path.exists() and path.read_text() == content:
        return False
    path.write_text(content)
    return True


def generate():
    text = REFLECTION_H.read_text()
    protos, unmatched = parse_prototypes(text)
    if unmatched:
        raise SystemExit(
            "gen_reflection_ffi: found __builtin_* declaration(s) in "
            "reflection.h that PROTO_RE failed to parse -- fix the regex, "
            "don't silently drop a builtin from the FFI table: "
            + ", ".join(sorted(unmatched))
        )
    return protos


def main(check=None):
    if check is None:
        check = "--check" in sys.argv[1:]
    protos = generate()
    protos_content = render_protos_inc(protos)
    register_content = render_register_inc(protos)

    if check:
        stale = []
        for path, content in ((PROTOS_INC, protos_content), (REGISTER_INC, register_content)):
            if not path.exists() or path.read_text() != content:
                stale.append(path)
        if stale:
            print("gen_reflection_ffi: stale/missing generated file(s):")
            for path in stale:
                print(f"  {path.relative_to(REPO_ROOT)}")
            print("Run: python3 tools/gen_reflection_ffi.py")
            return 1
        print(f"gen_reflection_ffi: up to date ({len(protos)} builtins)")
        return 0

    changed = []
    if write_if_changed(PROTOS_INC, protos_content):
        changed.append(PROTOS_INC)
    if write_if_changed(REGISTER_INC, register_content):
        changed.append(REGISTER_INC)
    if changed:
        print(f"gen_reflection_ffi: regenerated {len(changed)} file(s) "
              f"({len(protos)} builtins):")
        for path in changed:
            print(f"  {path.relative_to(REPO_ROOT)}")
    else:
        print(f"gen_reflection_ffi: already up to date ({len(protos)} builtins)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
