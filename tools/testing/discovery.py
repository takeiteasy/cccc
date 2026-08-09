"""Test file discovery for the CCCC test runner."""

import fnmatch
from pathlib import Path


def discover_tests(tests_dir, match=None, suites=False, legacy=False):
    """Return a sorted list of test_*.c Path objects under tests_dir.

    Excludes any path containing a 'host' or 'fuzz' component. 'host' holds
    host-side harnesses (see tests/host/) that link directly against the
    compiler sources and are built/run via `make host-tests`, not guest
    sources compiled by cccc through the exit-code-42 protocol. 'fuzz' holds
    the fuzz regression corpus (tests/fuzz/corpus/) which tools/fuzz_replay.py
    compile-only replays; a hand-added test_*.c reproducer there must never
    be picked up as a normal test. There is deliberately no 'failures'-style
    exclusion: a negative test belongs directly under tests/ (or tests/macros/)
    marked EXPECT_COMPILE_ERROR/EXPECT_RUNTIME_ERROR so discovery actually
    runs it, not in a subdirectory discovery silently skips.
    Exactly one of match/suites/legacy may be active at a time.
    """
    test_files = sorted(
        f for f in Path(tests_dir).rglob("test_*.c")
        if "host" not in f.parts and "fuzz" not in f.parts
    )
    if match:
        test_files = [f for f in test_files if fnmatch.fnmatch(f.name, match)]
    elif suites:
        test_files = [f for f in test_files if "suites" in f.parts]
    elif legacy:
        test_files = [f for f in test_files if "suites" not in f.parts]
    return test_files
