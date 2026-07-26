#!/usr/bin/env python3
"""Audit src/stdlib/*.c FFI registrations against include/*.h declarations.

For every cc_register_cfunc()/cc_register_cfunc_ex() call, cross-checks
num_args, returns_double, and double_arg_mask against the declared C
signature of the wrapped function (as pulled from include/*.h). Also flags
declared-but-never-registered names.

This exists because several registrations in src/stdlib/math.c (and a few
in stdlib.c/wide.c) had hand-typo'd num_args/returns_double/double_arg_mask
that silently produced garbage at runtime (#777). Run after any edit to
include/*.h or src/stdlib/*.c registration tables.

Limitations: this is a regex-based scanner, not a real C parser. It only
understands registrations of the form
    cc_register_cfunc[_ex](vm, "name", (void*)funcname, N, R[, MASK])
where funcname matches a declaration in include/*.h. Calls that wrap a
local helper (not itself declared in include/) or use function-pointer
expressions it can't parse are silently skipped, not reported as clean --
so a clean run means "everything this tool could check was consistent",
not "everything is definitely correct". Variadic declarations ("...")
are skipped since num_args there is only the fixed prefix by convention.

Exit code is nonzero iff a mismatch or an unregistered declaration was
found, so this can gate CI once it has proven stable in practice (see
follow-up filed alongside #777).
"""
import glob
import re
import sys

HEADER_GLOB = "include/*.h"
STDLIB_GLOB = "src/stdlib/*.c"

DECL_RE = re.compile(
    r'^\s*(?:extern\s+)?'
    r'((?:const\s+|unsigned\s+|signed\s+|long\s+|short\s+|struct\s+|union\s+)*'
    r'\w+[\w \*]*?)\s+'
    r'(\w+)\s*\(([^)]*)\)\s*;',
    re.MULTILINE,
)

REG_RE = re.compile(
    r'cc_register_cfunc(_ex)?\(\s*vm\s*,\s*"([^"]+)"\s*,\s*'
    r'\(void\*\)\s*(\w+)\s*,\s*(\d+)\s*,\s*(\d+)'
    r'(?:\s*,\s*([0-9a-fA-Fbx]+))?\s*\)',
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


def load_declarations():
    decls = {}
    for path in sorted(glob.glob(HEADER_GLOB)):
        text = strip_comments(open(path).read())
        for m in DECL_RE.finditer(text):
            ret, name, argstr = m.group(1).strip(), m.group(2), m.group(3).strip()
            if name in decls:
                continue  # first declaration wins (e.g. redeclared under #if)
            args = [] if argstr in ('', 'void') else [a.strip() for a in argstr.split(',')]
            decls[name] = (ret, args, path)
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

        for m in FAMILY_MACRO_RE.finditer(text):
            base = m.group(1)
            registered |= {base, base + 'f', base + 'l'}

    registered |= BUILTIN_SPECIAL_CASED

    # A header is "in scope" for the missing-registration check only if at
    # least one of its declared functions is registered somewhere -- this
    # avoids false positives on headers that aren't meant to be FFI-wired
    # at all (pure type/macro headers, or ones registered via a bespoke
    # mechanism this regex-based scanner can't see).
    in_scope_headers = {hdr for name, (ret, args, hdr) in decls.items() if name in registered}

    missing = []
    for name, (ret, args, hdr) in sorted(decls.items()):
        if hdr in in_scope_headers and name not in registered and not any(a == '...' for a in args):
            missing.append((name, ret, hdr))

    return problems, missing


def main():
    problems, missing = audit()
    if not problems and not missing:
        print("audit_ffi: no mismatches found")
        return 0

    for path, name, target, probs in problems:
        print(f"{path}: \"{name}\" (-> {target})")
        for p in probs:
            print(f"    {p}")

    for name, ret, hdr in missing:
        print(f"{hdr}: '{name}' declared but never registered")

    print(f"\naudit_ffi: {len(problems)} mismatch(es), {len(missing)} unregistered declaration(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
