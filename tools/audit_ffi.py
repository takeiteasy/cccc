#!/usr/bin/env python3
"""Audit src/stdlib/*.c FFI registrations against include/*.h declarations.

For every cc_register_cfunc()/cc_register_cfunc_ex()/cc_register_variadic_cfunc()
call, cross-checks num_args, returns_double, and double_arg_mask against the
declared C signature of the wrapped function (as pulled from include/**/*.h).
Also flags declared-but-never-registered names, and headers that declare at
least one function but register none at all (the #792 class: a guest that
includes only that header gets the declarations but nothing actually wired
into the FFI layer).

This exists because several registrations in src/stdlib/math.c (and a few
in stdlib.c/wide.c) had hand-typo'd num_args/returns_double/double_arg_mask
that silently produced garbage at runtime (#777), and because flock/ioctl/
statfs/fstatfs were declared in include/sys/*.h but never registered anywhere,
producing a runtime "undefined function" the moment a guest included only
that header (#792). Run after any edit to include/*.h or src/stdlib/*.c
registration tables.

Limitations: this is a regex-based scanner, not a real C parser. It only
understands registrations of the form
    cc_register_cfunc[_ex](vm, "name", (void*)funcname, N, R[, MASK])
    cc_register_variadic_cfunc(vm, "name", (void*)funcname, N, R)
where funcname matches a declaration in include/**/*.h. Calls that wrap a
local helper (not itself declared in include/) or use function-pointer
expressions it can't parse are silently skipped from the signature check,
not reported as clean -- so a clean run means "everything this tool could
check was consistent", not "everything is definitely correct". Variadic
declarations ("...") and cc_register_variadic_cfunc targets are exempt from
the num_args/mask check since num_args there is only the fixed prefix by
convention, but they still count as registered for the missing-registration
and zero-registered-header checks below.

Exit code is nonzero iff a mismatch, an unregistered declaration, or a
zero-registered header (not in COMPILER_LOWERED_HEADERS) was found. Wired
into `make audit-ffi` and the `audit_ffi` sub-suite of `make test` (#784).
"""
import glob
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
HEADER_GLOB = str(REPO_ROOT / "include" / "**" / "*.h")
STDLIB_GLOB = str(REPO_ROOT / "src" / "stdlib" / "*.c")

# include/cccc/*.h are compiler-internal builtin headers (reflection.h,
# testing.h, building.h) whose declarations are consumed directly by the
# compiler's comptime/testing/build machinery, not registered as FFI calls
# at all -- discover_headers() in tools/generate_stdlib.c skips them the
# same way (they're added to the stdlib.tsv scan manually, by name).
SKIP_HEADER_DIRS = ("cccc/",)

DECL_RE = re.compile(
    r'^\s*(?:extern\s+)?'
    r'((?:const\s+|unsigned\s+|signed\s+|long\s+|short\s+|struct\s+|union\s+)*'
    r'\w+[\w \*]*?)\s+'
    r'(\w+)\s*\(([^)]*)\)\s*;',
    re.MULTILINE,
)

REG_RE = re.compile(
    r'cc_register_cfunc(_ex)?\(\s*vm\s*,\s*"([^"]+)"\s*,\s*'
    r'\(\s*void\s*\*\s*\)\s*(\w+)\s*,\s*(\d+)\s*,\s*(\d+)'
    r'(?:\s*,\s*([0-9a-fA-Fbx]+))?\s*\)',
)

# cc_register_variadic_cfunc's num_args is only the fixed prefix (the "..."
# tail isn't counted), so these are exempt from the signature check but
# still count as registered -- same treatment as a variadic declaration.
VARIADIC_RE = re.compile(
    r'cc_register_variadic_cfunc\(\s*vm\s*,\s*"([^"]+)"\s*,\s*'
    r'\(\s*void\s*\*\s*\)\s*(\w+)',
)

# Family-registration macros (e.g. CCCC_REG_FMAXIMUM_FAMILY(fmaximum) in
# src/stdlib/math.c) expand to registrations for NAME/NAMEf/NAMEl via a
# wrapped cccc_##NAME target that isn't itself declared in include/*.h, so
# REG_RE's per-call signature check naturally no-ops on them (target not in
# decls). But they must still count as "registered" for the missing-decl
# check below, or every family member falsely reports as unregistered.
FAMILY_MACRO_RE = re.compile(r'CCCC_REG_\w+_FAMILY\((\w+)\)')

# Functions intentionally never FFI-registered: handled as compiler
# builtins/special-cased codegen instead (e.g. raise() gets inlined
# handling in codegen.c since it needs to interact with the VM's signal
# machinery directly, not a plain FFI call).
BUILTIN_SPECIAL_CASED = {"raise"}

# Headers whose declared functions are lowered directly by the compiler
# (dedicated opcodes / codegen special-casing) rather than going through
# src/stdlib/*.c's cc_register_cfunc table at all, so they legitimately
# have zero registrations -- without this allowlist every one of these
# would trip the zero-registered-header check below.
COMPILER_LOWERED_HEADERS = {
    # dlopen/dlsym/dlclose lower to a dedicated opcode (src/codegen.c,
    # is_extern_func_name(..., "dlclose") etc.; src/parse.c builtin_dlclose).
    "dlfcn.h",
    # stdc_leading_zeros_ui and friends are C23 <stdbit.h> builtins lowered
    # in codegen, not FFI calls.
    "stdbit.h",
}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//.*', '', text)
    return text


def param_kind(t):
    t = t.replace('const', '').strip()
    if '*' in t or '[' in t:
        return 'ptr'
    if t == 'float':
        return 'float'
    if t in ('double', 'long double'):
        return 'double'
    return 'int'


def header_rel(path):
    """Path relative to include/, e.g. 'sys/file.h' or 'fcntl.h'."""
    return str(Path(path).resolve().relative_to(REPO_ROOT / "include"))


def load_declarations():
    decls = {}
    for path in sorted(glob.glob(HEADER_GLOB, recursive=True)):
        rel = header_rel(path)
        if any(rel.startswith(d) for d in SKIP_HEADER_DIRS):
            continue
        text = strip_comments(open(path).read())
        for m in DECL_RE.finditer(text):
            ret, name, argstr = m.group(1).strip(), m.group(2), m.group(3).strip()
            if name in decls:
                continue  # first declaration wins (e.g. redeclared under #if)
            args = [] if argstr in ('', 'void') else [a.strip() for a in argstr.split(',')]
            decls[name] = (ret, args, rel)
    return decls


def expected_return_kind(ret):
    return {'double': 1, 'float': 2, 'int': 0, 'ptr': 0}[param_kind(ret)]


def expected_mask(args):
    return sum(1 << i for i, a in enumerate(args) if param_kind(a) == 'double')


def audit():
    decls = load_declarations()
    problems = []
    registered = set()

    for path in sorted(glob.glob(STDLIB_GLOB)):
        text = strip_comments(open(path).read())
        for m in REG_RE.finditer(text):
            _ex, name, target, na, rd, mask = m.groups()
            registered.add(name)
            # Only check when the FFI target itself is a declared libc/libm
            # function -- calls wrapping a local static helper aren't in
            # decls and are silently skipped (see module docstring).
            if target not in decls:
                continue
            ret, args, hdr = decls[target]
            if any(a == '...' for a in args):
                continue
            probs = []
            if int(na) != len(args):
                probs.append(f"num_args={na}, expected {len(args)} (from {args})")
            exp_rd = expected_return_kind(ret)
            if int(rd) != exp_rd:
                probs.append(f"returns_double={rd}, expected {exp_rd} (declared return '{ret}')")
            exp_mask = expected_mask(args)
            got_mask = int(mask, 0) if mask else 0
            if got_mask != exp_mask:
                probs.append(f"double_arg_mask={got_mask:#b}, expected {exp_mask:#b}")
            if probs:
                problems.append((path, name, target, probs))

        for m in VARIADIC_RE.finditer(text):
            name = m.group(1)
            registered.add(name)

        for m in FAMILY_MACRO_RE.finditer(text):
            base = m.group(1)
            registered |= {base, base + 'f', base + 'l'}

    registered |= BUILTIN_SPECIAL_CASED

    # A header is "in scope" for the missing-registration check only if at
    # least one of its declared functions is registered somewhere -- this
    # avoids false positives on headers that aren't meant to be FFI-wired
    # at all (pure type/macro headers, or ones registered via a bespoke
    # mechanism this regex-based scanner can't see).
    headers_with_decls = {hdr for name, (ret, args, hdr) in decls.items()}
    in_scope_headers = {hdr for name, (ret, args, hdr) in decls.items() if name in registered}

    missing = []
    for name, (ret, args, hdr) in sorted(decls.items()):
        if hdr in in_scope_headers and name not in registered and not any(a == '...' for a in args):
            missing.append((name, ret, hdr))

    # A header that declares at least one function but registers none at
    # all is the #792 bug class: guest code that includes only that header
    # compiles clean but fails at call time. Headers whose functions are
    # lowered directly by the compiler (COMPILER_LOWERED_HEADERS) are exempt.
    zero_registered = sorted(
        hdr for hdr in headers_with_decls
        if hdr not in in_scope_headers and hdr not in COMPILER_LOWERED_HEADERS
    )

    return problems, missing, zero_registered


def main():
    problems, missing, zero_registered = audit()
    if not problems and not missing and not zero_registered:
        print("audit_ffi: no mismatches found")
        return 0

    for path, name, target, probs in problems:
        print(f"{path}: \"{name}\" (-> {target})")
        for p in probs:
            print(f"    {p}")

    for name, ret, hdr in missing:
        print(f"{hdr}: '{name}' declared but never registered")

    for hdr in zero_registered:
        print(f"{hdr}: declares functions but registers none (guest including only "
              f"this header would fail at call time -- see #792)")

    print(f"\naudit_ffi: {len(problems)} mismatch(es), {len(missing)} unregistered "
          f"declaration(s), {len(zero_registered)} zero-registered header(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
