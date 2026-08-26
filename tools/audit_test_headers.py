#!/usr/bin/env python3
"""Audit tests/**/*.c header comments for CCCC_*/EXPECT_* directive damage.

#1153: the test runner used to read only the first 5 physical lines of a
test file with a bare substring scan, so a directive landing on line 6+ (or
truncated by a clang-format wrap onto a second `//` line, or merged into an
adjacent prose sentence) was silently never read -- the assertion it names
became vacuously true rather than failing loudly. tools/testing/header.py
fixed the parser itself (whole-header-block, anchored-directive scan); this
script is the recurrence guard, run as part of `tools/run_tests.py` (the
`test` build target) so a newly-introduced instance of the same class of bug
fails CI instead of silently passing.

Checks, each independently reported and each causing a nonzero exit:

  1. unanchored: a known `CCCC_*:`/`EXPECT_*` name appears in the header
     comment block but not as the anchored first token of its line -- the
     shape of a wrapped/merged directive that the parser will never see.
  2. after-header: a known directive appears in the file body, after the
     leading comment block ends -- also never seen by the parser.
  3. unknown-directive: a `CCCC_<ALLCAPS>:`-shaped comment that doesn't match
     any name in tools/testing/header.py's ALL_DIRECTIVES (typo guard), or a
     bare (no-colon) `CCCC_`/`EXPECT_`/`REJECT_`-shaped token in the header
     block that is a near-miss of a known directive -- Levenshtein distance
     <= 2, or the same underscore-separated components in a different order
     (e.g. `CCCC_SKIP_NATIVE` vs. the real `CCCC_NATIVE_SKIP`, #1158). A
     colon-required directive misspelled with its colon is caught by the
     first half of this check; a *bare*-spelled directive's typo (e.g.
     `EXPECT_COMPILE_ERR`, `CCCC_NATIVE_SKP`) has no colon to anchor on, so
     it was invisible to both the parser and this audit before the near-miss
     half was added -- the exact silently-ignored-directive failure class
     #1153 fixed for a different symptom.
  4. bad-regex: an EXPECT_*/REJECT_* STDOUT/STDERR value that doesn't compile
     as a Python regex (catches a truncation that lopped off a `\\[`/`\\]` or
     similar).
  5. empty-value: a value-requiring directive (CCCC_FLAGS/RUN_ARGS/
     EXPECT_STDERR/REJECT_STDERR/EXPECT_STDOUT/REJECT_STDOUT) with nothing
     after the colon.

Usage:
    python3 tools/audit_test_headers.py [--report-only] [tests/]

--report-only prints findings but always exits 0 -- used while repairing the
existing corpus (#1153 step 4) so the script is useful before every finding
is fixed. Without it (the default, and what `test` wires in), any finding is
a hard failure.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from testing.header import ALL_DIRECTIVES, _iter_header_lines, _anchor_match

_VALUE_REQUIRED = {
    "CCCC_FLAGS", "CCCC_RUN_ARGS", "CCCC_EXPECT_STDERR", "CCCC_REJECT_STDERR",
    "CCCC_EXPECT_STDOUT", "CCCC_REJECT_STDOUT",
}
_REGEX_VALUED = {
    "CCCC_EXPECT_STDERR", "CCCC_REJECT_STDERR",
    "CCCC_EXPECT_STDOUT", "CCCC_REJECT_STDOUT",
}

# Matches a *known* directive name as a whole word -- used to find
# unanchored/after-header instances of directives the parser actually
# recognises. Built from ALL_DIRECTIVES rather than a generic
# `CCCC_[A-Z0-9_]+` scan so it can't be confused by prose (this codebase has
# many internal preprocessor macros/flag names spelled CCCC_ALLCAPS, e.g.
# CCCC_HAS_DECIMAL, CCCC_CHECKED_BOUNDS, CCCC_TYPE_CHECKS -- none of them
# directives, none of them wanted here). A colon-required name (CCCC_FLAGS
# and the like, see header.py's _VALUE_DIRECTIVES) only counts as
# directive-shaped when it actually carries a colon -- otherwise it was
# never going to be recognised as a directive even if anchored (e.g. "listed
# first via CCCC_FLAGS so ..."), so flagging it would just relitigate
# ordinary prose. A bare-ok name (EXPECT_RUNTIME_ERROR and the like) matches
# with or without a colon, matching what the real parser would accept.
_COLON_REQUIRED_TOKEN_RE = re.compile(
    r'\b(' + '|'.join(re.escape(n) for n in sorted(_VALUE_REQUIRED, key=len, reverse=True)) + r'):'
)
_BARE_OK_NAMES = ALL_DIRECTIVES - _VALUE_REQUIRED
_BARE_OK_TOKEN_RE = re.compile(
    r'\b(' + '|'.join(re.escape(n) for n in sorted(_BARE_OK_NAMES, key=len, reverse=True)) + r')\b:?'
)


def _directive_shaped_names(text):
    """Yield every known-directive name in text that is at least shaped
    like a directive use (colon-required names only count with a colon;
    bare-ok names count either way)."""
    for m in _COLON_REQUIRED_TOKEN_RE.finditer(text):
        yield m.group(1)
    for m in _BARE_OK_TOKEN_RE.finditer(text):
        yield m.group(1)
# Any `CCCC_ALLCAPS:` token (colon required) -- candidate for the
# unknown-directive typo check. The colon is required because that's the
# one syntactic feature every real directive has and no internal macro name
# mentioned in prose does (verified against the corpus).
_TOKEN_RE = re.compile(r'\bCCCC_[A-Z0-9_]+:')

# #1158: a bare-spelled directive typo (e.g. `CCCC_NATIVE_SKP`,
# `EXPECT_COMPILE_ERR`) has no colon for _TOKEN_RE to anchor on, so it's
# invisible to the check above even though a bare-ok real directive
# (EXPECT_RUNTIME_ERROR and friends, header.py's _BARE_DIRECTIVES) is a
# legitimate no-colon spelling. This wider scan also covers EXPECT_/REJECT_
# names (not just CCCC_) since those prefixes are directive-shaped too
# (EXPECT_RUNTIME_ERROR, CCCC_REJECT_STDOUT). It is deliberately run over
# header-block lines only -- the file body is full of legitimate CCCC_*
# preprocessor macros (CCCC_HAS_DECIMAL, CCCC_CHECKED_BOUNDS, ...) that
# would otherwise need to individually clear the near-miss threshold, and
# the existing after-header check already covers *known* names leaking past
# the header block.
_NEAR_MISS_TOKEN_RE = re.compile(r'\b(?:CCCC|EXPECT|REJECT)_[A-Z0-9_]+\b')

# Corpus-verified false-positive margin (#1158): scanning every
# (CCCC|EXPECT|REJECT)_[A-Z0-9_]+ token in every tests/**/*.c header block,
# the minimum Levenshtein distance from a non-directive token to a real
# directive name is 4 (CCCC_NATIVE_CC vs. CCCC_NATIVE_SKIP). A threshold of
# 2 has margin to spare and no real directive-shaped prose token in the
# corpus is a component permutation of a directive name either.
_NEAR_MISS_MAX_DISTANCE = 2


def _levenshtein(a, b):
    if a == b:
        return 0
    if not a:
        return len(b)
    if not b:
        return len(a)
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1,        # deletion
                           cur[j - 1] + 1,      # insertion
                           prev[j - 1] + (ca != cb)))  # substitution
        prev = cur
    return prev[-1]


def _nearest_directive(tok):
    """Return (name, distance) for the ALL_DIRECTIVES entry closest to tok
    by Levenshtein distance."""
    return min(((name, _levenshtein(tok, name)) for name in ALL_DIRECTIVES),
               key=lambda pair: pair[1])


def _component_permutation_of(tok):
    """Return the ALL_DIRECTIVES name whose underscore-separated components
    are a reordering of tok's (e.g. CCCC_SKIP_NATIVE vs. the real
    CCCC_NATIVE_SKIP), or None -- a near-miss shape Levenshtein distance
    alone can miss, since reordering three-plus components often exceeds
    distance 2."""
    parts = sorted(tok.split("_"))
    for name in ALL_DIRECTIVES:
        if parts == sorted(name.split("_")):
            return name
    return None


def _line_anchor_pairs(content):
    """Return the (name, value_or_None) pairs the real parser would
    recognise on this single header-comment line, via header.py's own
    _anchor_match -- so "is this anchored" always agrees with the parser
    actually used at test-run time."""
    found = []
    remainder = content
    while remainder:
        matched = _anchor_match(remainder)
        if not matched:
            break
        m, name, value = matched
        found.append((name, value))
        if value is not None:
            break  # a `: value` directive owns the rest of the line
        remainder = remainder[m.end():].lstrip()
    return found


def audit_file(path):
    """Return a list of (kind, line_no, message) findings for one file."""
    findings = []
    header_line_count = 0

    for line_no, content in _iter_header_lines(path):
        header_line_count = line_no
        anchored = _line_anchor_pairs(content)
        anchored_names = {name for name, _ in anchored}

        colon_flagged = set()
        for m in _TOKEN_RE.finditer(content):
            name = m.group(0).rstrip(":")
            if name not in ALL_DIRECTIVES:
                findings.append(("unknown-directive", line_no,
                                  f"unrecognised directive-shaped token {name!r}"))
                colon_flagged.add(name)

        for m in _NEAR_MISS_TOKEN_RE.finditer(content):
            tok = m.group(0)
            if tok in ALL_DIRECTIVES or tok in colon_flagged:
                continue  # exact match, or already reported above
            nearest, distance = _nearest_directive(tok)
            if distance <= _NEAR_MISS_MAX_DISTANCE:
                findings.append(("unknown-directive", line_no,
                                  f"{tok!r} looks like a misspelled "
                                  f"{nearest!r} (edit distance {distance})"))
                continue
            permuted = _component_permutation_of(tok)
            if permuted:
                findings.append(("unknown-directive", line_no,
                                  f"{tok!r} looks like {permuted!r} with "
                                  "components in the wrong order"))

        for name in _directive_shaped_names(content):
            if name not in anchored_names:
                findings.append(("unanchored", line_no,
                                  f"{name} appears but is not the anchored "
                                  "start of its line (wrapped or merged into "
                                  "prose -- the parser will never see it)"))

        for name, value in anchored:
            if name not in _VALUE_REQUIRED:
                continue
            if not (value or "").strip():
                findings.append(("empty-value", line_no,
                                  f"{name} has no value after the colon"))
                continue
            if name in _REGEX_VALUED:
                try:
                    re.compile(value)
                except re.error as e:
                    findings.append(("bad-regex", line_no,
                                      f"{name} value is not a valid regex: {e}"))

    # after-header: scan the rest of the file for directive-shaped tokens
    # that a human might expect the parser to honour but never appear in the
    # leading comment block at all.
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError:
        return findings
    for line_no, line in enumerate(lines[header_line_count:], start=header_line_count + 1):
        for name in _directive_shaped_names(line):
            findings.append(("after-header", line_no,
                              f"{name} appears after the header comment "
                              "block ends -- never read by the parser"))
    return findings


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("testdir", nargs="?", default="tests",
                     help="Directory of test files to audit (default: tests/)")
    ap.add_argument("--report-only", action="store_true",
                     help="Print findings but always exit 0")
    args = ap.parse_args(argv)

    testdir = Path(args.testdir)
    files = sorted(testdir.rglob("*.c"))

    total_findings = 0
    for f in files:
        findings = audit_file(f)
        for kind, line_no, message in findings:
            print(f"{f}:{line_no}: [{kind}] {message}")
            total_findings += 1

    if total_findings:
        print(f"\n{total_findings} finding(s) across {len(files)} files audited.")
    else:
        print(f"No findings across {len(files)} files audited.")

    if args.report_only:
        return 0
    return 1 if total_findings else 0


if __name__ == "__main__":
    sys.exit(main())
