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

Exit code is nonzero iff a mismatch, an unregistered declaration, a
zero-registered header (not in COMPILER_LOWERED_HEADERS), or a guard
mismatch (see below) was found. Wired into `make audit-ffi` and the
`audit_ffi` sub-suite of `make test` (#784).

Guard-presence check (#869): a declaration can be wrapped in a preprocessor
conditional (`#ifdef __linux__`, `#if defined(__linux__) ||
defined(__CCCC_POSIX_EMULATION__)`, ...), and independently, each place a
name is registered can be wrapped in its own conditional. This tool tracks
only whether each side is guarded by *some* condition or not -- it does not
attempt to decide whether two non-empty conditions are the same platform
test, since that would require a real preprocessor evaluator, and a
same-named macro can mean different things in different evaluation
contexts anyway (see the `--posix-emulation` exemption below). What it does
catch: a name declared under a real (non-boilerplate) conditional in its
header but registered completely unconditionally somewhere, or declared
completely unconditionally but only ever registered under a conditional --
both are the shape of a #792-class bug (a guest can see -- or fail to see
-- a declaration that doesn't match what got wired into the FFI table).
Each file's own outermost wrapper (a header's `#ifndef X_H` include guard,
or `src/stdlib/posix_*.c`/`pthread.c`'s whole-file `#if !defined(_WIN32) &&
!defined(_WIN64)`) is detected and excluded automatically -- see
strip_common_wrapper()'s docstring -- so it isn't mistaken for a real,
per-symbol platform guard.

Registration sites that sit inside a preprocessor conditional *and* are
gated a second time by a `vm->flags &` runtime check (the `--posix-emulation`
pattern used by ppoll/sched_setparam & co., src/stdlib/posix_poll.c/posix_sched.c) are exempt
from this check entirely, reported separately as "runtime-flag-gated,
not checked" rather than silently passed or falsely flagged: the runtime
flag corresponds to a macro (`__CCCC_POSIX_EMULATION__`) defined for the
*guest*'s own preprocessor at guest-compile time, which is a wholly
different evaluation context from `posix.c`'s own one-time host compile,
so there is no sound way for this tool to relate the two.
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

# Preprocessor conditions that only mean something to the *guest*'s own
# preprocessor (a feature-test macro CCCC's front-end defines while parsing
# guest source, e.g. __STDC_IEC_60559_DFP__ for C23 decimal support), with
# no host-build-time counterpart posix.c/decimal_math.c could mirror --
# same fundamental mismatch as the --posix-emulation runtime-flag pattern
# (see RUNTIME_GATE_MARKER), just without a textual runtime-check marker to
# detect it by. __cccc_dec_math1/2/3/i/n/p and __cccc_dec_strtod are
# declared in include/*.h only under __STDC_IEC_60559_DFP__, but their host
# wrapper always exists (a real BID-library implementation when built with
# CCCC_HAS_DECIMAL, a clean ENOSYS-shaped stub otherwise) and is always
# registered, by design -- the dispatch entry point must exist regardless
# of whether a given guest translation unit happens to see the feature-test
# macro. A declaration guard whose text matches one of these is exempt from
# the guard-presence check entirely, the same way a runtime-gated
# registration is.
#
# __CCCC__ (#1021): include/fenv.h and include/errno.h wrap their whole
# CCCC-flavored body in `#ifdef __CCCC__ ... #else #include_next <fenv.h
# /errno.h> #endif`. __CCCC__ is always defined while CCCC's own
# preprocessor parses guest source (init_macros(), src/preprocess.c) --
# these declarations are unconditional from the guest's point of view, and
# only take the #else branch when a real host compiler reprocesses this
# exact file during -c=native/-c=generated serializer replay, at which
# point the corresponding functions are declared by the host's own
# #include_next'd header instead. Same guest-only/host-build-time mismatch
# as __STDC_IEC_60559_DFP__ above; src/stdlib/fenv.c's registrations are
# unconditional, so without this the guard-presence check would misreport
# an "always guarded" declaration against an "always registered" C.
GUEST_ONLY_DECL_GUARDS = {"__STDC_IEC_60559_DFP__", "__CCCC__"}

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


COND_LINE_RE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')


def scan_conditionals(text):
    """Return a dict mapping 1-indexed line number -> the active
    preprocessor guard stack at the start of that line, as a tuple of
    condition strings (outermost first).

    #elif/#else replace the current frame with the negated/alternate
    condition rather than modeling true sibling-branch semantics -- adequate
    for spotting presence/absence of a guard, not for evaluating it.
    """
    lines = text.split('\n')
    stack = []  # list of display strings
    line_guard = {}
    for i, line in enumerate(lines, start=1):
        line_guard[i] = tuple(stack)
        m = COND_LINE_RE.match(line)
        if m:
            kind, rest = m.group(1), m.group(2).strip()
            if kind == 'if':
                stack.append(re.sub(r'\s+', ' ', rest))
            elif kind == 'ifdef':
                stack.append(rest)
            elif kind == 'ifndef':
                stack.append(f'!{rest}')
            elif kind == 'elif':
                if stack:
                    stack.pop()
                stack.append(re.sub(r'\s+', ' ', rest))
            elif kind == 'else':
                if stack:
                    # Negate whatever text this frame's own #if/#ifdef/
                    # #ifndef used, so collapse_exhaustive_guards can match
                    # an if-branch's display against its else-branch's by
                    # exact string negation regardless of which directive
                    # spelling opened the frame.
                    stack.append(f"!({stack.pop()})")
            elif kind == 'endif':
                if stack:
                    stack.pop()
    return line_guard


def collapse_exhaustive_guards(guards):
    """Collapse `#if COND ... #else ... #endif` pairs that both register the
    same name: scan_conditionals gives the else-branch's frame the display
    text `!(COND)`, so two guard tuples sharing an identical prefix whose
    last elements are COND and `!(COND)` jointly cover every case -- replace
    both with just the shared prefix (dropping that frame) and repeat to a
    fixed point, so deeper nested if/else pairs collapse outward too. This
    only recognizes a *textually* exact condition/negation pair (the same
    shape scan_conditionals produces for a plain #if/#else, not #elif chains
    or differently-worded but logically-equivalent conditions), so it's a
    conservative "don't flag what's obviously exhaustive", not a general
    proof of coverage.
    """
    guards = list(guards)
    changed = True
    while changed:
        changed = False
        groups = {}
        empties = [g for g in guards if not g]
        for g in guards:
            if g:
                groups.setdefault(g[:-1], []).append(g[-1])
        next_guards = list(empties)
        for prefix, lasts in groups.items():
            lastset = set(lasts)
            paired = set()
            for c in list(lastset):
                neg = f"!({c})"
                if neg in lastset and c not in paired and neg not in paired:
                    paired.add(c)
                    paired.add(neg)
                    next_guards.append(prefix)
                    changed = True
            for c in lastset - paired:
                next_guards.append(prefix + (c,))
        guards = next_guards
    return guards


def strip_common_wrapper(entries):
    """Given [(name, guard_tuple), ...] all drawn from one source file, drop
    the outermost frame from every guard if -- and only if -- every entry
    that has any guard at all shares the identical outermost condition. That
    identifies a whole-file wrapper (a header's own `#ifndef` include guard,
    or `posix.c`/`pthread.c`'s `#if !defined(_WIN32) && !defined(_WIN64)`)
    rather than a real per-symbol platform test, without needing to
    special-case either idiom by name. A file with no such uniform outer
    frame (declarations/registrations at mixed depths, e.g. math.c's
    occasional standalone `#ifdef __APPLE__`) is returned unchanged.
    """
    guarded = [g for _, g in entries if g]
    if not guarded:
        return entries
    first_frames = {g[0] for g in guarded}
    if len(first_frames) != 1 or len(guarded) != len(entries):
        return entries
    return [(name, g[1:]) for name, g in entries]


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
        line_guard = scan_conditionals(text)
        file_decls = []  # (name, ret, args, guard)
        for m in DECL_RE.finditer(text):
            ret, name, argstr = m.group(1).strip(), m.group(2), m.group(3).strip()
            args = [] if argstr in ('', 'void') else [a.strip() for a in argstr.split(',')]
            lineno = text.count('\n', 0, m.start()) + 1
            guard = line_guard.get(lineno, ())
            file_decls.append((name, ret, args, guard))
        stripped = strip_common_wrapper([(n, g) for n, _, _, g in file_decls])
        for (name, ret, args, _), (_, guard) in zip(file_decls, stripped):
            if name in decls:
                continue  # first declaration wins (e.g. redeclared under #if)
            decls[name] = (ret, args, rel, guard)
    return decls


def expected_return_kind(ret):
    return {'double': 1, 'float': 2, 'int': 0, 'ptr': 0}[param_kind(ret)]


def expected_mask(args):
    return sum(1 << i for i, a in enumerate(args) if param_kind(a) == 'double')


RUNTIME_GATE_MARKER = "vm->flags &"
# How many lines above a registration call to search for RUNTIME_GATE_MARKER.
# The real pattern (src/stdlib/posix_poll.c/posix_sched.c's ppoll/sched_setparam et al.) is
# "if (vm->flags & CCCC_POSIX_EMULATION) {" one line above a small run of
# registration calls; this just needs to comfortably cover that block.
# posix_sched.c's --posix-emulation block registers 5 functions (some spanning
# 2 lines each), so the last one (sched_rr_get_interval) sits 9 lines below
# the "if" -- 8 was one short and produced a false-positive guard mismatch on
# exactly that entry; bumped to comfortably cover a run this long.
RUNTIME_GATE_LOOKBACK_LINES = 12


def audit():
    decls = load_declarations()
    problems = []
    registered = set()
    # name -> [(path, guard_tuple, runtime_gated), ...], one entry per
    # registration occurrence, for the guard-presence check below.
    reg_occurrences = {}

    for path in sorted(glob.glob(STDLIB_GLOB)):
        text = strip_comments(open(path).read())
        lines = text.split('\n')
        line_guard = scan_conditionals(text)
        file_regs = []  # (name, guard, runtime_gated) for this file's strip pass

        def record(name, m):
            lineno = text.count('\n', 0, m.start()) + 1
            guard = line_guard.get(lineno, ())
            # Bounded lookback rather than "since this frame opened": at
            # shallow nesting (e.g. just inside a whole-file wrapper) the
            # enclosing frame can span thousands of lines, so searching its
            # full text would pick up an unrelated "vm->flags &" from
            # distant, unconnected code. The real pattern here is always
            # "if (vm->flags & ...) { <registration> }" a line or two above.
            window_start = max(0, lineno - 1 - RUNTIME_GATE_LOOKBACK_LINES)
            window = '\n'.join(lines[window_start:lineno])
            runtime_gated = bool(guard) and RUNTIME_GATE_MARKER in window
            file_regs.append((name, guard, runtime_gated))

        for m in REG_RE.finditer(text):
            _ex, name, target, na, rd, mask = m.groups()
            registered.add(name)
            record(name, m)
            # Only check when the FFI target itself is a declared libc/libm
            # function -- calls wrapping a local static helper aren't in
            # decls and are silently skipped (see module docstring).
            if target not in decls:
                continue
            ret, args, hdr, _decl_guard = decls[target]
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
            record(name, m)

        for m in FAMILY_MACRO_RE.finditer(text):
            base = m.group(1)
            registered |= {base, base + 'f', base + 'l'}

        stripped = strip_common_wrapper([(n, g) for n, g, _ in file_regs])
        for (name, _, runtime_gated), (_, guard) in zip(file_regs, stripped):
            reg_occurrences.setdefault(name, []).append((path, guard, runtime_gated))

    registered |= BUILTIN_SPECIAL_CASED

    # A header is "in scope" for the missing-registration check only if at
    # least one of its declared functions is registered somewhere -- this
    # avoids false positives on headers that aren't meant to be FFI-wired
    # at all (pure type/macro headers, or ones registered via a bespoke
    # mechanism this regex-based scanner can't see).
    headers_with_decls = {hdr for name, (ret, args, hdr, guard) in decls.items()}
    in_scope_headers = {hdr for name, (ret, args, hdr, guard) in decls.items() if name in registered}

    missing = []
    for name, (ret, args, hdr, guard) in sorted(decls.items()):
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

    # Guard-presence check (#869): see module docstring. Only meaningful for
    # names this tool can actually see a declaration for. Occurrences are
    # collapsed (see collapse_exhaustive_guards) before judging, so a name
    # registered via a complete #if/#else pair -- covering every case, just
    # via two different code paths -- reads as unconditional, not as two
    # separate "guarded" occurrences.
    guard_mismatches = []
    runtime_gated_count = 0
    for name, occurrences in sorted(reg_occurrences.items()):
        if name not in decls:
            continue
        _ret, _args, hdr, decl_guard = decls[name]
        if any(cond in GUEST_ONLY_DECL_GUARDS for cond in decl_guard):
            continue
        decl_is_guarded = bool(decl_guard)

        checkable = [(path, guard) for path, guard, runtime_gated in occurrences if not runtime_gated]
        runtime_gated_count += len(occurrences) - len(checkable)
        if not checkable:
            continue
        paths = sorted({path for path, _ in checkable})
        collapsed = collapse_exhaustive_guards([guard for _, guard in checkable])
        registered_unconditionally = any(not g for g in collapsed)

        if decl_is_guarded and registered_unconditionally:
            guard_mismatches.append(
                (paths, name, hdr, "declared conditionally but registered unconditionally"))
        elif not decl_is_guarded and not registered_unconditionally:
            conditions = sorted({g[-1] for g in collapsed if g})
            guard_mismatches.append(
                (paths, name, hdr,
                 f"declared unconditionally but only registered under: {', '.join(conditions)}"))

    return problems, missing, zero_registered, guard_mismatches, runtime_gated_count


def main():
    problems, missing, zero_registered, guard_mismatches, runtime_gated_count = audit()
    if not problems and not missing and not zero_registered and not guard_mismatches:
        print("audit_ffi: no mismatches found")
        if runtime_gated_count:
            print(f"({runtime_gated_count} runtime-flag-gated registration(s) not checked "
                  f"for guard presence -- see module docstring)")
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

    for paths, name, hdr, reason in guard_mismatches:
        where = ", ".join(str(Path(p).relative_to(REPO_ROOT)) for p in paths)
        print(f"{where}: \"{name}\" (declared in {hdr}) -- {reason}")

    print(f"\naudit_ffi: {len(problems)} mismatch(es), {len(missing)} unregistered "
          f"declaration(s), {len(zero_registered)} zero-registered header(s), "
          f"{len(guard_mismatches)} guard mismatch(es)")
    if runtime_gated_count:
        print(f"({runtime_gated_count} runtime-flag-gated registration(s) not checked "
              f"for guard presence -- see module docstring)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
