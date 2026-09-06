#!/usr/bin/env python3
"""Audit that every host-facing translation macro cccc's own source relies on
(#1315) is covered by src/host_shadow_macros.h/src/host_values.c, or is on
this script's documented allowlist.

Background: a handful of files under src/stdlib/ (posix_sched.c's
wrap_sysconf/wrap_pathconf/wrap_fpathconf/wrap_confstr and its sched-policy
pair, posix_poll.c's guest_to_host_pollev/host_to_guest_pollev, locale.c's
guest_to_host_lc/guest_to_host_lc_mask) translate CCCC's canonical guest
numbering to the *real* host's numbering via `#ifdef REAL_MACRO`/plain
references. Under self-hosting (cccc compiling its own src/*.c), CCCC's
own bundled headers shadow those macro names with the canonical (guest)
values instead of the real host's -- collapsing the translation to
identity. src/host_shadow_macros.h + src/host_values.c fix the currently
known set; this script keeps that set from silently growing stale as
CCCC's bundled headers or cccc's own source change.

Method, two checks:

1. For every name `include/*.h` `#define`s, check whether it is also
   `#define`d by the real system headers src/stdlib/posix_util.h pulls in
   (via the host cc's `-E -dM`) with a *different* value, AND is
   referenced by cccc's own source (src/**/*.c, src/**/*.h, excluding the
   generated src/std.c and the text-only src/shims/*.c). Any such name
   must appear in src/host_shadow_macros.h's table -- otherwise it's a
   silent, untested shadowing hazard exactly like #1315's original three.
2. Any src/stdlib/*.c file that references a name the shim covers must
   itself #include the shim (directly, or transitively via
   posix_util.h) -- otherwise a future translation-table file that
   forgets the #include reopens #1315's exact bug class for that file
   alone, invisible to check 1 (which only notices an uncovered *name*,
   not a whole missing #include).

This audit is inherently host-platform-specific (the divergent set differs
between macOS and glibc) -- run it on each platform CI actually covers
before relying on a clean run elsewhere on it alone.

Usage:
    python3 tools/audit_host_macro_shadow.py          # human-readable report
    python3 tools/audit_host_macro_shadow.py --check  # nonzero exit on any gap
"""
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
INCLUDE_DIR = REPO_ROOT / "include"
SRC_DIR = REPO_ROOT / "src"
POSIX_UTIL_H = SRC_DIR / "stdlib" / "posix_util.h"
SHADOW_H = SRC_DIR / "host_shadow_macros.h"

# Deliberately-divergent VM-model constants: part of no canonical<->host
# translation table, so their divergence from the real host is intentional,
# not a bug. Keep this list short and comment every entry.
ALLOWLIST = {
    # include/limits.h: CCCC's own guest stack/path-buffer sizing constant,
    # not derived from (or fed into) any host libc call.
    "PATH_MAX",
    # include/unistd.h: the POSIX/X/Open version CCCC claims to implement,
    # kept in sync with the feature-test macros init_macros() predefines --
    # not a value ever passed to a real host function.
    "_XOPEN_VERSION",
    # posix_util.h's own #ifndef-guarded local fallback for a glibc
    # extension gated behind _GNU_SOURCE (absent on macOS entirely) --
    # shadowing+restoring here would just race that guard. See
    # src/host_shadow_macros.h's file comment.
    "SCHED_BATCH",
    "SCHED_IDLE",
}

MACRO_DEF_RE = re.compile(
    r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+([^\n\\]*)$", re.M
)
INCLUDE_ANGLE_RE = re.compile(r'^\s*#\s*include\s*<([^>]+)>', re.M)
IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def bundled_macro_defs():
    """name -> (header basename, resolved numeric-or-raw value) for every
    #define in include/*.h, as the REAL host cc would actually resolve it --
    not a naive first-match text scan, which would wrongly pick an
    `#ifdef __APPLE__`-guarded arm's value on Linux (a false-positive
    divergence report).

    One preprocess per bundled header (not `-nostdinc`, and not a single
    combined pass over all of them): `-I include` alone still resolves
    `#include <name.h>` to CCCC's bundled copy first (an `-I` path always
    wins over the compiler's own built-in system dirs), so a header's own
    `#include_next <name.h>` (the `__CCCC__`-absent hand-off arm several
    bundled headers carry -- stdio.h, locale.h, ...) correctly continues
    the search into the real system header afterwards instead of erroring
    ("file not found") the way it would under `-nostdinc` (which strips
    every system dir `#include_next` needs to resume from). Combining every
    bundled header into one probe fails for the same reason one level up:
    header A's own transitive chain can reach header B's `#include_next`
    before B is probed on its own, erroring out the whole combined pass
    with zero -dM output. Per-header keeps each header's own conditional
    arms independent and gives every `#include_next`-carrying header
    something real to hand off to.
    """
    # First, a raw scan purely to know which header a name lives in and
    # thus how to report it (host_macro_values() below has no notion of
    # "which bundled header" -- only names and values).
    header_of = {}
    for h in sorted(INCLUDE_DIR.glob("*.h")):
        text = h.read_text(errors="ignore")
        for m in MACRO_DEF_RE.finditer(text):
            header_of.setdefault(m.group(1), h.name)

    out = {}
    for h in sorted(INCLUDE_DIR.glob("*.h")):
        try:
            proc = subprocess.run(
                ["cc", "-E", "-dM", "-I", str(INCLUDE_DIR), "-x", "c", "-"],
                input=f"#include <{h.name}>\n",
                capture_output=True, text=True, timeout=30,
            )
        except Exception:
            continue
        if proc.returncode != 0:
            # A header this audit can't resolve at all contributes nothing
            # -- silently, same as a name genuinely absent from any header --
            # rather than risk comparing a stale/partial value.
            continue
        for m in re.finditer(r"^#define ([A-Za-z_][A-Za-z0-9_]*) (.*)$", proc.stdout, re.M):
            name, rhs = m.group(1), m.group(2).strip()
            if name in header_of and name not in out:
                out[name] = (header_of[name], rhs)
    return out


def referenced_identifiers():
    """Every identifier referenced anywhere in cccc's own source, excluding
    the generated src/std.c and the text-only (never-compiled) src/shims/
    source-of-truth files."""
    ids = set()
    for pat in ("*.c", "*.h"):
        for p in SRC_DIR.rglob(pat):
            if p.name == "std.c":
                continue
            if "shims" in p.relative_to(SRC_DIR).parts[:-1] and p.parent.name == "shims":
                continue
            ids.update(IDENT_RE.findall(p.read_text(errors="ignore")))
    return ids


def host_macro_values():
    """name -> raw #define RHS text from the real system headers
    posix_util.h's system-include block pulls in, via the host cc."""
    text = POSIX_UTIL_H.read_text(errors="ignore")
    headers = INCLUDE_ANGLE_RE.findall(text)
    probe = "".join(f"#include <{h}>\n" for h in headers)
    try:
        out = subprocess.run(
            ["cc", "-E", "-dM", "-x", "c", "-"],
            input=probe,
            capture_output=True,
            text=True,
            timeout=30,
        ).stdout
    except Exception as e:
        print(f"audit_host_macro_shadow: could not run host cc probe: {e}")
        return {}
    result = {}
    for m in re.finditer(r"^#define ([A-Za-z_][A-Za-z0-9_]*) (.*)$", out, re.M):
        result[m.group(1)] = m.group(2).strip()
    return result


def numeric(v):
    """Parse a macro's RHS as a plain integer literal.

    KNOWN GAP: glibc defines the whole _SC_*/_PC_*/_CS_* family as an enum,
    with each name then re-#define'd to itself (`#define _SC_PAGESIZE
    _SC_PAGESIZE`) purely so `#ifdef _SC_PAGESIZE` still works, and defines
    LC_* as one level of #define indirection onto a separate, differently-
    named enum (`#define LC_ALL __LC_ALL`, `__LC_ALL` itself the enum
    constant). Either way `-dM` prints the textual RHS verbatim (a bare
    self-reference, or the *other* macro's name) rather than the
    underlying integer, so this returns None for it here and find_gaps()
    silently skips comparing it -- on glibc hosts this audit cannot
    numerically verify the _SC_*/LC_* families at all, even though
    src/host_values.c's actual injector (which compiles real C, not a
    textual scan) resolves the true value correctly regardless.
    Confirmed by hand (Linux container, #1315): a real self-hosted `-E` on
    posix_sched.c correctly baked in `sysconf(30)` (glibc's real
    _SC_PAGESIZE enum value), not the canonical `11`. This gap in the
    audit is why tools/header_resolution_smoke.py's case 14 checks these
    same two names a different way -- compiling and running a one-line
    probe rather than scanning -dM text -- and that case does correctly
    catch a regression in either family on a glibc host where this audit
    cannot.
    """
    try:
        return int(v, 0)
    except (TypeError, ValueError):
        return None


def shadow_covered_names():
    """Names src/host_shadow_macros.h already restores -- every identifier
    following `#undef ` in the file."""
    text = SHADOW_H.read_text(errors="ignore")
    return set(re.findall(r"^#undef\s+([A-Za-z_][A-Za-z0-9_]*)", text, re.M))


def find_gaps():
    bundled = bundled_macro_defs()
    referenced = referenced_identifiers()
    host = host_macro_values()
    covered = shadow_covered_names()

    gaps = []
    for name, (header, bundled_rhs) in bundled.items():
        if name not in referenced:
            continue
        if name not in host:
            continue
        a, b = numeric(bundled_rhs), numeric(host[name])
        if a is None or b is None or a == b:
            continue
        # Divergent, referenced by our own source. Must be covered or
        # allowlisted.
        if name in covered or name in ALLOWLIST:
            continue
        gaps.append((name, header, a, b))
    return gaps


# Any src/stdlib/*.c file whose text mentions one of these gets the shim
# for free (it #include's posix_util.h, which already carries it) -- so
# find_missing_includes() below doesn't need to actually resolve #include
# chains, just recognize the one indirect path that isn't a direct
# `host_shadow_macros.h` include.
TRANSITIVELY_COVERED_MARKER = '"posix_util.h"'


def find_missing_includes():
    """A file that references a name src/host_shadow_macros.h covers, but
    never includes the shim itself (directly or via posix_util.h), would
    silently reopen #1315's exact bug class for that file alone -- this
    audit's numeric divergence check above only catches an uncovered
    *name*, not a whole new translation-table file that forgot the
    #include entirely. Heuristic, not a real #include-chain resolution:
    good enough to catch the shape of mistake #1315 itself was."""
    covered = shadow_covered_names()
    if not covered:
        return []

    missing = []
    for p in sorted((SRC_DIR / "stdlib").glob("*.c")):
        text = p.read_text(errors="ignore")
        has_shim = ('"../host_shadow_macros.h"' in text or
                    "host_shadow_macros.h" in text or
                    TRANSITIVELY_COVERED_MARKER in text)
        if has_shim:
            continue
        names_used = set(IDENT_RE.findall(text)) & covered
        if names_used:
            missing.append((p.relative_to(REPO_ROOT).as_posix(), sorted(names_used)))
    return missing


def main(check=None):
    if check is None:
        check = "--check" in sys.argv[1:]

    if not host_macro_values():
        msg = "audit_host_macro_shadow: no host cc available, skipping"
        print(msg)
        return 0

    gaps = find_gaps()
    missing = find_missing_includes()
    if not gaps and not missing:
        print("audit_host_macro_shadow: no uncovered host-macro shadowing gaps")
        return 0

    if gaps:
        print("audit_host_macro_shadow: uncovered host-macro shadowing gaps found:")
        for name, header, bundled_v, host_v in sorted(gaps):
            print(f"  {name:<28} {header:<14} bundled={bundled_v!s:<10} host={host_v}")
        print()
        print("Each name above is #define'd differently by include/*.h (CCCC's")
        print("bundled, canonical-numbered header) than by the real system")
        print("header, and is referenced by cccc's own source -- add it to")
        print("src/host_shadow_macros.h + src/host_values.c's table, or to this")
        print("script's ALLOWLIST with a comment explaining why it's exempt.")

    if missing:
        if gaps:
            print()
        print("audit_host_macro_shadow: file(s) use a shadow-covered name but")
        print("never include the shim (directly, or via posix_util.h):")
        for path, names in missing:
            print(f"  {path}: {', '.join(names)}")
        print()
        print("Add `#include \"../host_shadow_macros.h\"` to each file above --")
        print("otherwise self-hosting can shadow these names right back to")
        print("CCCC's own canonical values for that file alone (#1315).")

    return 1 if check else 0


if __name__ == "__main__":
    sys.exit(main())
