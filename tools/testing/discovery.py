"""Test file discovery for the CCCC test runner."""

import fnmatch
from pathlib import Path


def discover_tests(tests_dir, match=None, suites=False, legacy=False):
    """Return a sorted list of test_*.c Path objects under tests_dir.

    Excludes any path containing a 'failures' component.
    Exactly one of match/suites/legacy may be active at a time.
    """
    test_files = sorted(
        f for f in Path(tests_dir).rglob("test_*.c") if "failures" not in f.parts
    )
    if match:
        test_files = [f for f in test_files if fnmatch.fnmatch(f.name, match)]
    elif suites:
        test_files = [f for f in test_files if "suites" in f.parts]
    elif legacy:
        test_files = [f for f in test_files if "suites" not in f.parts]
    return test_files
