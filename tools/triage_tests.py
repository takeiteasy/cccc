#!/usr/bin/env python3
"""
tools/triage_tests.py — classify tests/test_*.c for [[cccc::test]] migration.

Outputs a grouped report and optional JSON dump.

Usage:
    python3 tools/triage_tests.py [--json] [tests/]
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

# ── annotation regexes (checked in first 5 lines, matching tests.py behaviour)
RE_COMPILE_ERROR  = re.compile(r'\bEXPECT_COMPILE_ERROR\b')
RE_RUNTIME_ERROR  = re.compile(r'\bEXPECT_RUNTIME_ERROR\b')
RE_EXPECT_STDERR  = re.compile(r'\bCCCC_EXPECT_STDERR\s*:')
RE_EXPECT_STDOUT  = re.compile(r'\bCCCC_EXPECT_STDOUT\s*:')
RE_REJECT_STDERR  = re.compile(r'\bCCCC_REJECT_STDERR\s*:')
RE_REJECT_STDOUT  = re.compile(r'\bCCCC_REJECT_STDOUT\s*:')
RE_FLAGS          = re.compile(r'\bCCCC_FLAGS\s*:\s*(.+)')
RE_TESTING_FLAG   = re.compile(r'--testing\b')
RE_BUILD_FLAG     = re.compile(r'--build\b')
RE_OUTPUT_FLAG    = re.compile(r'\s(-E|-M|-G)\b')

# ── body heuristics (whole file)
RE_PRINTF         = re.compile(r'\bprintf\s*\(')
RE_SCANF          = re.compile(r'\bscanf\s*\(|\bgetchar\s*\(|\bfgets\s*\(|\bgets\s*\(')
RE_MALLOC         = re.compile(r'\bmalloc\s*\(|\bcalloc\s*\(|\brealloc\s*\(')
RE_FREE           = re.compile(r'\bfree\s*\(')
RE_FORK_EXEC      = re.compile(r'\bfork\s*\(|\bexec[lv]?\w*\s*\(')
RE_EXIT_EXPLICIT  = re.compile(r'\bexit\s*\(')
RE_SIGNAL_USE     = re.compile(r'\bsignal\s*\(|\bsigaction\s*\(')
RE_FILE_IO        = re.compile(r'\bfopen\s*\(|\bfclose\s*\(|\bfread\s*\(|\bfwrite\s*\(')
RE_RETURN_FAIL    = re.compile(r'\breturn\s+[01]\s*;')
RE_RETURN_42      = re.compile(r'\breturn\s+42\s*;')
# detect printf used as the primary assertion (printing computed values to stdout)
RE_PRINTF_VALUE   = re.compile(r'\bprintf\s*\([^)]*%[diouxXeEfgGcs]')


def read_header(path, n=5):
    """Return first n lines of file as a single string."""
    lines = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for i, line in enumerate(f):
            if i >= n:
                break
            lines.append(line)
    return ''.join(lines)


def read_all(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def classify(path):
    """Return (category, notes) for a single test file."""
    hdr  = read_header(path)
    body = read_all(path)

    has_compile_error  = bool(RE_COMPILE_ERROR.search(hdr))
    has_runtime_error  = bool(RE_RUNTIME_ERROR.search(hdr))
    has_expect_stderr  = bool(RE_EXPECT_STDERR.search(hdr))
    has_expect_stdout  = bool(RE_EXPECT_STDOUT.search(hdr))
    has_reject_stderr  = bool(RE_REJECT_STDERR.search(hdr))
    has_reject_stdout  = bool(RE_REJECT_STDOUT.search(hdr))

    flags_match = RE_FLAGS.search(hdr)
    flags_str   = flags_match.group(1).strip() if flags_match else ''
    has_testing  = bool(RE_TESTING_FLAG.search(flags_str))
    has_build    = bool(RE_BUILD_FLAG.search(flags_str))
    has_output   = bool(RE_OUTPUT_FLAG.search(flags_str))

    notes = []

    # ── already migrated
    if has_testing:
        return 'already_framework', []

    # ── must-stay: build mode
    if has_build:
        return 'must_stay_build', []

    # ── must-stay: output/preprocess mode
    if has_output:
        return 'must_stay_output', []

    # ── must-stay: stdout matching (no runtime stdout match in framework)
    if has_expect_stdout or has_reject_stdout:
        return 'must_stay_stdout', []

    # ── must-stay: runtime stderr matching
    #    (compile-error tests with CCCC_EXPECT_STDERR are partially migratable,
    #    but the framework does substring match, not regex — keep external for now)
    if has_expect_stderr or has_reject_stderr:
        return 'must_stay_stderr', []

    # ── migratable: runtime error (EXPECT_RUNTIME_ERROR, no stderr constraint)
    #    Requires fork fix (testing.c:1003) — already applied.
    if has_runtime_error:
        return 'migratable_runtime_error', []

    # ── migratable: compile error (EXPECT_COMPILE_ERROR, no stderr constraint)
    if has_compile_error:
        return 'migratable_compile_error', []

    # ── pure behavioral tests — classify by complexity
    has_printf_value = bool(RE_PRINTF_VALUE.search(body))
    has_printf       = bool(RE_PRINTF.search(body))
    has_scanf        = bool(RE_SCANF.search(body))
    has_malloc       = bool(RE_MALLOC.search(body))
    has_free         = bool(RE_FREE.search(body))
    has_fork         = bool(RE_FORK_EXEC.search(body))
    has_exit         = bool(RE_EXIT_EXPLICIT.search(body))
    has_signal       = bool(RE_SIGNAL_USE.search(body))
    has_file_io      = bool(RE_FILE_IO.search(body))
    has_return_42    = bool(RE_RETURN_42.search(body))

    # boundary: process/OS-level behaviour that doesn't translate
    if has_fork or has_scanf or has_signal or has_file_io:
        if has_fork:    notes.append('fork/exec')
        if has_scanf:   notes.append('stdin I/O')
        if has_signal:  notes.append('signal handler')
        if has_file_io: notes.append('file I/O')
        return 'boundary', notes

    if has_exit:
        notes.append('explicit exit()')
        return 'boundary', notes

    if not has_return_42:
        notes.append('no return 42 found')
        return 'boundary', notes

    # requires adaptation: uses printf for value observation or malloc
    if has_printf_value:
        notes.append('printf with format args (value observation)')
        return 'requires_adaptation', notes

    if has_malloc or has_free:
        notes.append('dynamic allocation')
        return 'requires_adaptation', notes

    if has_printf:
        notes.append('printf (no format args)')
        return 'requires_adaptation', notes

    return 'trivially_convertible', []


CATEGORY_ORDER = [
    'trivially_convertible',
    'requires_adaptation',
    'boundary',
    'migratable_runtime_error',
    'migratable_compile_error',
    'must_stay_stderr',
    'must_stay_stdout',
    'must_stay_build',
    'must_stay_output',
    'already_framework',
]

CATEGORY_LABEL = {
    'trivially_convertible':   'Trivially convertible (mechanical rewrite)',
    'requires_adaptation':     'Requires adaptation (printf / malloc / multi-scenario)',
    'boundary':                'Boundary (stdin / fork / signal / file I/O)',
    'migratable_runtime_error':'Migratable — runtime error (exit_code=255)',
    'migratable_compile_error':'Migratable — compile error (error="...")',
    'must_stay_stderr':        'Must stay external — stderr/stdout matching (regex)',
    'must_stay_stdout':        'Must stay external — stdout matching',
    'must_stay_build':         'Must stay external — --build mode',
    'must_stay_output':        'Must stay external — -E/-M/-G mode',
    'already_framework':       'Already in [[cccc::test]] framework',
}


def main():
    ap = argparse.ArgumentParser(description='Triage test_*.c files for [[cccc::test]] migration')
    ap.add_argument('testdir', nargs='?', default='tests', help='Directory of test files (default: tests/)')
    ap.add_argument('--json', action='store_true', help='Emit full JSON report to stdout')
    ap.add_argument('--category', metavar='CAT', help='Only list files in this category')
    args = ap.parse_args()

    testdir = Path(args.testdir)
    files = sorted(testdir.glob('test_*.c'))

    results = {}
    by_category = {c: [] for c in CATEGORY_ORDER}

    for f in files:
        cat, notes = classify(f)
        entry = {'file': str(f), 'category': cat, 'notes': notes}
        results[str(f)] = entry
        by_category[cat].append(entry)

    if args.json:
        json.dump(results, sys.stdout, indent=2)
        print()
        return

    if args.category:
        items = by_category.get(args.category, [])
        for it in items:
            suffix = f"  [{', '.join(it['notes'])}]" if it['notes'] else ''
            print(f"  {it['file']}{suffix}")
        print(f"\n{len(items)} tests in category '{args.category}'")
        return

    total = len(files)
    print(f"CCCC Test Triage Report — {total} test files\n")
    print(f"{'Category':<55} {'Count':>6}  {'%':>5}")
    print('-' * 70)

    for cat in CATEGORY_ORDER:
        items = by_category[cat]
        pct   = 100 * len(items) / total if total else 0
        print(f"{CATEGORY_LABEL[cat]:<55} {len(items):>6}  {pct:>4.1f}%")

    print('-' * 70)
    print(f"{'TOTAL':<55} {total:>6}  100.0%")

    migratable = (len(by_category['trivially_convertible']) +
                  len(by_category['requires_adaptation']) +
                  len(by_category['migratable_runtime_error']) +
                  len(by_category['migratable_compile_error']))
    print(f"\nMigratable (all categories): {migratable} / {total}")

    print("\n── Trivially convertible (pilot candidates) ──")
    for it in by_category['trivially_convertible']:
        print(f"  {it['file']}")

    print("\n── Migratable runtime errors ──")
    for it in by_category['migratable_runtime_error']:
        suffix = f"  [{', '.join(it['notes'])}]" if it['notes'] else ''
        print(f"  {it['file']}{suffix}")

    print("\n── Boundary / adaptation notes ──")
    for cat in ('requires_adaptation', 'boundary'):
        for it in by_category[cat]:
            suffix = f"  [{', '.join(it['notes'])}]" if it['notes'] else ''
            print(f"  {it['file']}{suffix}")


if __name__ == '__main__':
    main()
