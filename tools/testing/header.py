"""Shared parser for the CCCC_* / EXPECT_* test-header directive comments
that lead every tests/**/*.c file.

Historically this parsing was duplicated three ways (tools/testing/runner.py's
inline parse in run_single_test(), and tools/triage_tests.py's own regex re-implementation), all reading
only the first 5 physical lines of the file with a bare substring scan. Any
directive landing on line 6+ (because earlier lines carried longer prose) was
silently never read -- not an error, just absent, so e.g. a CCCC_REJECT_STDOUT
assertion became vacuously true. Separately, clang-format wrapping a long
directive comment onto a second `//` line silently truncated whatever the
scan did read (fixed for future files by .clang-format's CommentPragmas
guard, #1153).

This module reads the whole leading comment block instead of a fixed window,
but *only* recognises a directive when it is anchored -- the first token of
its comment line/paragraph, not merely a substring appearing anywhere. A
whole-block scan without anchoring would misfire on ordinary prose that
mentions a directive name in passing (e.g. "the EXPECT_RUNTIME_ERROR path" or
"CCCC_MATRIX_SKIP) -- unlike ..."), both of which occur in the corpus.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

# Directives that take no value -- bare presence is the whole signal.
_BARE_DIRECTIVES = frozenset({
    "EXPECT_COMPILE_ERROR",
    "EXPECT_RUNTIME_ERROR",
    "CCCC_LEAKS_KEEP_VM_HEAP",
})

# Directives that require a `: value` suffix.
_VALUE_DIRECTIVES = frozenset({
    "CCCC_FLAGS",
    "CCCC_RUN_ARGS",
    "CCCC_EXPECT_STDERR",
    "CCCC_REJECT_STDERR",
    "CCCC_EXPECT_STDOUT",
    "CCCC_REJECT_STDOUT",
})

# Directives that may appear bare (with a default reason) or with an
# explicit `: reason`.
_OPTIONAL_VALUE_DIRECTIVES = frozenset({
    "CCCC_NATIVE_SKIP",
    "CCCC_EXPECT_LEAK",
})

_DEFAULT_REASONS = {
    "CCCC_NATIVE_SKIP": "native-incompatible",
    "CCCC_EXPECT_LEAK": "expected leak",
}

ALL_DIRECTIVES = _BARE_DIRECTIVES | _VALUE_DIRECTIVES | _OPTIONAL_VALUE_DIRECTIVES

# Longest-name-first so e.g. CCCC_REJECT_STDOUT isn't shadowed by a shorter
# prefix match against CCCC_REJECT (there is none today, but keeps the
# anchor scan unambiguous as directives are added).
_DIRECTIVE_NAMES = sorted(ALL_DIRECTIVES, key=len, reverse=True)

_TRAILING_BLOCK_COMMENT_END = re.compile(r"\s*\*/\s*$")

# A directive that always requires a `: value` suffix (CCCC_FLAGS and the
# like) must NOT match bare -- "CCCC_FLAGS above, since ..." and "... listed
# first via CCCC_FLAGS so main.c's ..." both occur as ordinary prose in the
# corpus, and without this split the anchor rule would mistake either for an
# empty CCCC_FLAGS directive (first token of its comment line, followed by
# whitespace). Directives that are legitimately bare (EXPECT_COMPILE_ERROR,
# CCCC_LEAKS_KEEP_VM_HEAP) or optionally bare with a default
# reason (CCCC_NATIVE_SKIP, CCCC_EXPECT_LEAK) keep the
# original terminated-by-':'-or-whitespace-or-end anchor.
_COLON_REQUIRED_NAMES = sorted(_VALUE_DIRECTIVES, key=len, reverse=True)
_BARE_OK_NAMES = sorted(_BARE_DIRECTIVES | _OPTIONAL_VALUE_DIRECTIVES, key=len, reverse=True)

# Matches a single directive token anchored at the start of a comment's
# content: a colon-required NAME always needs ': value'; a bare-ok NAME
# either takes ': value' or nothing at all (terminated by end-of-string or
# whitespace so `CCCC_MATRIX_SKIP)` in prose is not a match).
# `EXPECT_COMPILE_ERROR`/`EXPECT_RUNTIME_ERROR` may be immediately followed
# by whitespace then a second directive on the same physical line (the
# widely-used `// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2` idiom) -- callers
# handle that by re-invoking the anchor match on the remainder after
# consuming a bare-flag hit.
_ANCHOR_RE = re.compile(
    r"^(?:"
    r"(" + "|".join(re.escape(n) for n in _COLON_REQUIRED_NAMES) + r"):\s*(.*)"
    r"|"
    r"(" + "|".join(re.escape(n) for n in _BARE_OK_NAMES) + r")(?::\s*(.*)|(?=\s|$))"
    r")"
)


@dataclass
class TestHeader:
    is_negative_test: bool = False
    expects_runtime_error: bool = False
    flags: list[str] = field(default_factory=list)
    run_args: list[str] = field(default_factory=list)
    expect_stderr: str | None = None
    reject_stderr: str | None = None
    expect_stdout: str | None = None
    reject_stdout: str | None = None
    native_skip: str | None = None
    expect_leak: str | None = None
    leaks_keep_vm_heap: bool = False

    # Raw (name, value_or_None) pairs in file order, for audit tooling.
    raw: list[tuple[str, str | None]] = field(default_factory=list)

    @property
    def is_testing_mode(self) -> bool:
        return "--testing" in self.flags

    @property
    def is_build_mode(self) -> bool:
        return "--build" in self.flags


_BLANK = object()  # sentinel: a blank line, distinct from "not a comment"


def _strip_comment_marker(line: str):
    """Classify one line: return _BLANK for a blank line, None for a real
    (non-comment, non-blank) code line, or the comment's content with its
    leading marker stripped."""
    s = line.rstrip("\n")
    stripped = s.strip()
    if not stripped:
        return _BLANK
    if stripped.startswith("//"):
        return stripped[2:].strip()
    if stripped.startswith("/*"):
        return stripped[2:].strip()
    if stripped.startswith("*") and not stripped.startswith("*/"):
        # continuation line inside a /* ... */ block, conventionally
        # leading with " * "
        return stripped[1:].strip()
    return None


def _iter_header_lines(path, max_lines: int = 200):
    """Yield (line_no, content) for the leading header block's comment
    lines, 1-indexed by physical file line, marker stripped. The header
    block is the leading run of comment lines and blank lines; blank lines
    are skipped (not yielded, but still counted) rather than ending the
    scan, since an explanatory paragraph followed by a blank line followed
    by the actual directive block is a common, legitimate layout (e.g.
    tests/test_pragma_comment_lib.c). The scan stops at the first genuine
    code line, or max_lines, whichever comes first."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f, start=1):
            if i > max_lines:
                return
            content = _strip_comment_marker(line)
            if content is _BLANK:
                continue
            if content is None:
                return
            yield i, content


def _anchor_match(remainder: str):
    """Match _ANCHOR_RE against remainder and normalise its two possible
    branches (colon-required name, or bare-ok name) into (match, name,
    value_or_None). Returns None if there is no anchored directive here."""
    m = _ANCHOR_RE.match(remainder)
    if not m:
        return None
    if m.group(1) is not None:
        return m, m.group(1), m.group(2)
    return m, m.group(3), m.group(4)


def parse_test_header(path) -> TestHeader:
    """Parse the leading comment-block header of a tests/**/*.c file into a
    TestHeader. Unlike the historical 5-line scan, this reads the whole
    header block but only recognises directives that are anchored (the
    first token of a comment line), so prose mentioning a directive name in
    passing is not mistaken for the directive itself."""
    header = TestHeader()
    for _line_no, content in _iter_header_lines(path):
        # A line may legitimately carry two anchored directives, e.g.
        # `EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2`. Loop until the remainder no
        # longer starts with a recognised directive.
        remainder = content
        while remainder:
            matched = _anchor_match(remainder)
            if not matched:
                break
            m, name, value = matched
            if value is not None:
                value = _TRAILING_BLOCK_COMMENT_END.sub("", value).strip()
                # A directive requiring no value (bare) doesn't consume a
                # trailing ':' the same way -- but since bare directives
                # aren't in the ':'-branch of the regex, this only happens
                # for VALUE/OPTIONAL_VALUE directives, which is correct.
                header.raw.append((name, value))
                _apply(header, name, value)
                remainder = ""  # a `: value` directive owns the rest of the line
            else:
                header.raw.append((name, None))
                _apply(header, name, None)
                # consume the matched token and any following whitespace,
                # continue scanning the same line for a second directive
                remainder = remainder[m.end():].lstrip()
    return header


def _apply(header: TestHeader, name: str, value: str | None) -> None:
    if name == "EXPECT_COMPILE_ERROR":
        header.is_negative_test = True
    elif name == "EXPECT_RUNTIME_ERROR":
        header.expects_runtime_error = True
    elif name == "CCCC_LEAKS_KEEP_VM_HEAP":
        header.leaks_keep_vm_heap = True
    elif name == "CCCC_NATIVE_SKIP":
        header.native_skip = value if value else _DEFAULT_REASONS[name]
    elif name == "CCCC_EXPECT_LEAK":
        header.expect_leak = value if value else _DEFAULT_REASONS[name]
    elif name == "CCCC_FLAGS":
        header.flags = (value or "").split()
    elif name == "CCCC_RUN_ARGS":
        header.run_args = (value or "").split()
    elif name == "CCCC_EXPECT_STDERR":
        header.expect_stderr = value
    elif name == "CCCC_REJECT_STDERR":
        header.reject_stderr = value
    elif name == "CCCC_EXPECT_STDOUT":
        header.expect_stdout = value
    elif name == "CCCC_REJECT_STDOUT":
        header.reject_stdout = value
