#!/usr/bin/env python3
"""Test runner for CCCC — thin entry-point shim.

All logic lives in tools/testing/. This file exists so that:
  - 'python3 tools/tests.py ...' continues to work (Makefile targets, docs)
  - Cross-platform targets that pass --binary / --match directly still work
  - 'make test-suites', 'make test-legacy', etc. are unchanged

Arguments after -- are forwarded verbatim to the cccc binary for every test
(e.g. `tools/tests.py -- -O2 -Wall`).  Unknown flags before -- are an error.

With --matrix, runs the full suite once per individual -f optimization pass
(9 runs: baseline, one per pass, stress) and shows a per-pass attribution table.
"""

import sys
from pathlib import Path

# Ensure the tools/ directory is on sys.path so 'testing' is importable
# regardless of cwd (the cross-platform Makefile targets run from repo root).
sys.path.insert(0, str(Path(__file__).resolve().parent))

from testing.cli import main

if __name__ == "__main__":
    main()
