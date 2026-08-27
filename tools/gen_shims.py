#!/usr/bin/env python3
"""Generate src/shims.inc (the -c=native support-shim text table) from the
ordinary C in src/shims/*.c.

src/serialize_shims.c used to hold every -c=native shim body as a hand-escaped
C string literal passed to fprintf() -- ~114 chunks, no syntax highlighting, no
clang-format, and a dropped '\\n' only ever surfaced as a host-compiler error on
whichever guest program happened to use that one shim. The bodies now live as
real C under src/shims/, one file per emitter group, each chunk delimited by

    // >>> shim: <name>
    ...C...
    // <<< shim

This script turns each chunk into a `static const char CCCC_SHIM_<group>_<name>[]`
in src/shims.inc, which src/serialize_shims.c #includes and references by name --
so a typo'd chunk name is a compile error, not a silently-missing shim. Whole-line
`//` comments and blank lines at the top/bottom of a chunk are dropped; the emitted
text is otherwise byte-for-byte what the old string literals produced. `/* ... */`
is emitted verbatim.

src/shims.inc is committed (like src/reflection_ffi_*.inc, unlike src/std.c) so
plain `make` -- no python3 -- still satisfies the stage0 bootstrap invariant.
`./cccc --build build.c`'s default build regenerates it via the `shims_gen` step;
`--check` (wired into the `test` build target / tools/run_tests.py) fails the
build if the committed file is stale.

Usage:
    python3 tools/gen_shims.py          # write/refresh src/shims.inc
    python3 tools/gen_shims.py --check  # verify it is up to date
"""
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SHIMS_DIR = REPO_ROOT / "src" / "shims"
OUT_INC = REPO_ROOT / "src" / "shims.inc"

# emitter-group files, in the order sections appear in src/shims.inc
GROUPS = [
    "native_accessor", "reallocarray", "threads", "uchar",
    "posix_compat", "canonical_const", "dlfcn", "c23_fromfp",
]

BANNER = """\
/* GENERATED FILE -- DO NOT EDIT.
 * Produced by tools/gen_shims.py from the src/shims/ directory.
 * Regenerate with: python3 tools/gen_shims.py
 */
"""

OPEN_RE = re.compile(r"^//\s*>>>\s*shim:\s*(\S+)\s*$")
CLOSE_RE = re.compile(r"^//\s*<<<\s*shim\s*$")


def parse_group(group):
    """Return [(section_name, text), ...] for one src/shims/<group>.c."""
    path = SHIMS_DIR / f"{group}.c"
    out = []
    cur = None
    body = []
    for lineno, raw in enumerate(path.read_text().split("\n"), 1):
        m = OPEN_RE.match(raw)
        if m:
            if cur is not None:
                raise SystemExit(f"{path}:{lineno}: nested '>>> shim:' "
                                 f"(previous '{cur}' never closed)")
            cur = m.group(1)
            body = []
            continue
        if CLOSE_RE.match(raw):
            if cur is None:
                raise SystemExit(f"{path}:{lineno}: '<<< shim' with no open")
            # drop whole-line // comments; trim leading/trailing blank lines
            kept = [ln for ln in body if ln.strip() == "" or not ln.lstrip().startswith("//")]
            while kept and kept[0].strip() == "":
                kept.pop(0)
            while kept and kept[-1].strip() == "":
                kept.pop()
            if not kept:
                raise SystemExit(f"{path}: section '{cur}' is empty")
            out.append((cur, "".join(ln + "\n" for ln in kept)))
            cur = None
            continue
        if cur is not None:
            body.append(raw)
    if cur is not None:
        raise SystemExit(f"{path}: section '{cur}' never closed")
    return out


def c_escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def render_constant(symbol, text):
    # one string-literal per source line, matching how the old fprintf()
    # literals were physically laid out -- keeps the .inc reviewable.
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    parts = [f'"{c_escape(ln)}\\n"' for ln in lines]
    joined = ("\n    ").join(parts)
    return f"static const char {symbol}[] =\n    {joined};\n"


def pathconf_drift_check(sections):
    """DRY guard: canonical_const's `pathconf` and `fpathconf` sections share
    a 9-way _PC_* switch (was a local macro). Fail if they fall out of sync."""
    d = dict(sections.get("canonical_const", []))
    if "pathconf" not in d or "fpathconf" not in d:
        return
    def norm(t):
        pcs = sorted(re.findall(r"_PC_[A-Z_]+", t))
        cases = len(re.findall(r"case \d+:", t))
        return (pcs, cases)
    if norm(d["pathconf"]) != norm(d["fpathconf"]):
        raise SystemExit(
            "gen_shims: canonical_const pathconf/fpathconf _PC_* switches have "
            "drifted -- they must stay identical (add the case to both).")


def build():
    all_sections = {}
    parts = [BANNER]
    seen = set()
    for group in GROUPS:
        sections = parse_group(group)
        all_sections[group] = sections
        parts.append(f"\n/* ---- src/shims/{group}.c "
                     f"{'-' * (58 - len(group))} */\n")
        for name, text in sections:
            symbol = f"CCCC_SHIM_{group}_{name}"
            if symbol in seen:
                raise SystemExit(f"gen_shims: duplicate section {symbol}")
            seen.add(symbol)
            parts.append(render_constant(symbol, text))
    pathconf_drift_check(all_sections)
    n = sum(len(v) for v in all_sections.values())
    return "".join(parts), n


def main(check=None):
    if check is None:
        check = "--check" in sys.argv[1:]
    content, n = build()
    if check:
        if not OUT_INC.exists() or OUT_INC.read_text() != content:
            print("gen_shims: src/shims.inc is stale or missing.")
            print("Run: python3 tools/gen_shims.py")
            return 1
        print(f"gen_shims: up to date ({n} shim sections)")
        return 0
    if OUT_INC.exists() and OUT_INC.read_text() == content:
        print(f"gen_shims: already up to date ({n} shim sections)")
        return 0
    OUT_INC.write_text(content)
    print(f"gen_shims: wrote src/shims.inc ({n} shim sections)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
