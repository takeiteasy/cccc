#!/usr/bin/env python3
"""Unit tests for tools/testing/header.py's parse_test_header() (#1153) and
tools/audit_test_headers.py's audit_file() typo/near-miss detection (#1158).

Pure in-memory tests against temp files -- no cccc binary needed. Run as its
own audited sub-suite from tools/run_tests.py (mirrors audit_ffi.py's/
audit_test_headers.py's own main() -> 0/nonzero convention), and can also be
run standalone: `python3 tools/testing/test_header_parse.py`.

The audit_file() cases live here rather than under tests/ because a
deliberately-typo'd fixture file would make tools/audit_test_headers.py fail
its own CI run if it lived under the directory that script scans.
"""

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from testing.header import parse_test_header, ALL_DIRECTIVES
from audit_test_headers import audit_file


def _parse(text):
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        return parse_test_header(path)
    finally:
        Path(path).unlink()


def _audit(text):
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        return audit_file(Path(path))
    finally:
        Path(path).unlink()


CASES = []


def case(name):
    def deco(fn):
        CASES.append((name, fn))
        return fn
    return deco


@case("directive past the old 5-line window is still read")
def _(fail):
    h = _parse(
        "// line 1\n// line 2\n// line 3\n// line 4\n"
        "// line 5\n// line 6\n"
        "// CCCC_EXPECT_STDOUT: hello\n"
        "int main(void) { return 42; }\n"
    )
    if h.expect_stdout != "hello":
        fail(f"expected 'hello', got {h.expect_stdout!r}")


@case("EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2 on one line (widely-used idiom)")
def _(fail):
    h = _parse("// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2\nint main(void){return 42;}\n")
    if not h.expects_runtime_error:
        fail("expects_runtime_error not set")
    if h.flags != ["-2"]:
        fail(f"flags={h.flags!r}")


@case("/* ... */ block header, multi-line, closing */ alone")
def _(fail):
    h = _parse(
        "/* EXPECT_COMPILE_ERROR */\n"
        "/* CCCC_FLAGS: --std=c89 -Werror=pedantic */\n"
        "/* CCCC_EXPECT_STDERR: error: '//' comments are a C99 extension\n"
        " */\n"
        "int main(void) { return 42; }\n"
    )
    if not h.is_negative_test:
        fail("is_negative_test not set")
    if h.flags != ["--std=c89", "-Werror=pedantic"]:
        fail(f"flags={h.flags!r}")
    if h.expect_stderr != "error: '//' comments are a C99 extension":
        fail(f"expect_stderr={h.expect_stderr!r}")


@case("prose containing 'CCCC_MATRIX_SKIP)' mid-sentence is not a directive")
def _(fail):
    # tests/test_vla_heap_exhaustion.c's actual pre-repair shape (#1153).
    h = _parse(
        "// Deliberately no per-test flags override (and no\n"
        "// CCCC_MATRIX_SKIP) -- unlike the address-identity tests, this is robust\n"
        "int main(void) { return 42; }\n"
    )
    if h.matrix_skip is not None:
        fail(f"matrix_skip should be None, got {h.matrix_skip!r}")


@case("bare mention of a colon-required name (no colon) is not a directive")
def _(fail):
    # tests/test_attr_vector_size_fusion.c's actual pre-repair shape (#1153).
    h = _parse(
        "// Forced to -O4 via\n"
        "// CCCC_FLAGS above, since the default test run is unoptimized.\n"
        "int main(void) { return 42; }\n"
    )
    if h.flags != []:
        fail(f"flags should be empty, got {h.flags!r}")


@case("explanatory paragraph, blank line, then the directive block")
def _(fail):
    # tests/test_pragma_comment_lib.c's actual shape (#1153 blank-line fix).
    h = _parse(
        "// Some explanation here.\n"
        "// More explanation.\n"
        "\n"
        "// CCCC_FLAGS: -Wcpp\n"
        "// CCCC_EXPECT_STDERR: unknown pragma ignored\n"
        "\n"
        "int main(void) { return 42; }\n"
    )
    if h.flags != ["-Wcpp"]:
        fail(f"flags={h.flags!r}")
    if h.expect_stderr != "unknown pragma ignored":
        fail(f"expect_stderr={h.expect_stderr!r}")


@case("trailing '*/' is stripped without eating a legitimate trailing .*")
def _(fail):
    h = _parse(
        "/* CCCC_EXPECT_STDERR: case value not in enumerated type.*[-Wswitch] */\n"
        "int main(void) { return 42; }\n"
    )
    if h.expect_stderr != "case value not in enumerated type.*[-Wswitch]":
        fail(f"expect_stderr={h.expect_stderr!r} -- trailing regex text was eaten")


@case("a real code line (not blank) ends the header block")
def _(fail):
    h = _parse(
        "#include <stdlib.h>\n"
        "// CCCC_FLAGS: -O0\n"
        "int main(void) { return 42; }\n"
    )
    if h.flags != []:
        fail(f"flags should be empty (directive is after real code), got {h.flags!r}")


@case("repeated same-name directive: last one wins")
def _(fail):
    h = _parse(
        "// CCCC_EXPECT_STDOUT: first\n"
        "// CCCC_EXPECT_STDOUT: second\n"
        "int main(void) { return 42; }\n"
    )
    if h.expect_stdout != "second":
        fail(f"expect_stdout={h.expect_stdout!r}")


@case("every directive header.py recognises is a real, spelled-out name")
def _(fail):
    for name in ALL_DIRECTIVES:
        if not name.startswith(("CCCC_", "EXPECT_")):
            fail(f"suspicious directive name in ALL_DIRECTIVES: {name!r}")


@case("#1158: bare misspelled directive (edit distance) is caught")
def _(fail):
    findings = _audit("// CCCC_NATIVE_SKP\nint main(void) { return 42; }\n")
    kinds = [k for k, _, _ in findings]
    if "unknown-directive" not in kinds:
        fail(f"expected an unknown-directive finding, got {findings!r}")


@case("#1158: bare misspelled EXPECT_* directive is caught")
def _(fail):
    findings = _audit("// EXPECT_COMPILE_ERR\nint main(void) { return 42; }\n")
    kinds = [k for k, _, _ in findings]
    if "unknown-directive" not in kinds:
        fail(f"expected an unknown-directive finding, got {findings!r}")


@case("#1158: component-permutation directive typo is caught")
def _(fail):
    # The ticket's own repro: CCCC_SKIP_NATIVE vs. the real CCCC_NATIVE_SKIP
    # -- edit distance is too large for the Levenshtein half of the check,
    # so this only fires via the component-permutation half.
    findings = _audit("// CCCC_SKIP_NATIVE\nint main(void) { return 42; }\n")
    kinds = [k for k, _, _ in findings]
    if "unknown-directive" not in kinds:
        fail(f"expected an unknown-directive finding, got {findings!r}")


@case("#1158: a real bare directive is not flagged as its own near-miss")
def _(fail):
    findings = _audit("// EXPECT_RUNTIME_ERROR\nint main(void) { return 42; }\n")
    if findings:
        fail(f"expected no findings, got {findings!r}")


@case("#1158: internal CCCC_* macro names in prose are not false positives")
def _(fail):
    # CCCC_NATIVE_CC is the corpus's closest non-directive token to a real
    # directive name (edit distance 4 from CCCC_NATIVE_SKIP) -- well clear
    # of the threshold, and not a component permutation of any real name.
    findings = _audit(
        "// Uses CCCC_NATIVE_CC to pick the host compiler, and touches\n"
        "// CCCC_HAS_DECIMAL and CCCC_CHECKED_BOUNDS in the process.\n"
        "int main(void) { return 42; }\n"
    )
    if findings:
        fail(f"expected no findings, got {findings!r}")


def main():
    failures = []
    for name, fn in CASES:
        errors = []
        fn(errors.append)
        if errors:
            failures.append((name, errors))

    if not failures:
        print(f"test_header_parse: {len(CASES)} cases passed")
        return 0

    for name, errors in failures:
        print(f"FAILED: {name}")
        for e in errors:
            print(f"    {e}")
    print(f"\n{len(failures)}/{len(CASES)} cases failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
