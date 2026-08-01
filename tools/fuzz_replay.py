#!/usr/bin/env python3
"""Fuzz regression corpus replay (#625).

Compile-only replay of tests/fuzz/corpus/*.c against a cccc binary. Each
corpus file is a minimized reproducer for a crash a fuzzer found (or a
pathological input worth keeping); see tests/fuzz/README.md for the format.

Pass/fail (per corpus file):
  pass — the process exits normally, whether that's a clean compile or a
         clean diagnostic (the compiler is allowed to reject garbage).
  fail — killed by a signal (SIGSEGV/SIGBUS/SIGABRT/...), exceeds the
         timeout, or stderr contains an ASan/UBSan report.

An empty or missing corpus directory is a clean skip (counts as pass), so
this suite is green today and grows as reproducers accumulate.

Usage:
  python3 tools/fuzz_replay.py [--binary PATH] [--timeout SECONDS]
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CORPUS_DIR = REPO_ROOT / "tests" / "fuzz" / "corpus"

SANITIZER_RE = re.compile(r"(AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:)")


def replay_one(cccc, src, timeout):
    """Compile a single corpus file. Returns (ok, reason)."""
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "out.c4"
        try:
            r = subprocess.run(
                [str(cccc), "-I", str(REPO_ROOT / "include"), "-c", "-o", str(out), str(src)],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            return False, f"timeout (>{timeout}s)"

        if r.returncode < 0:
            import signal
            sig = signal.Signals(-r.returncode).name
            return False, f"killed by {sig}"

        if SANITIZER_RE.search(r.stderr):
            return False, "sanitizer report"

        return True, "ok"


def main(argv=None):
    parser = argparse.ArgumentParser(description="Replay the fuzz regression corpus")
    parser.add_argument("--binary", default=str(REPO_ROOT / "cccc"), help="path to cccc binary")
    parser.add_argument("--timeout", type=float, default=10.0, help="per-file timeout in seconds")
    args = parser.parse_args(argv)

    cccc = Path(args.binary)
    if not cccc.exists():
        print(f"Error: {cccc} not found. Run 'make' first.", file=sys.stderr)
        return 1

    if not CORPUS_DIR.exists():
        print("fuzz replay: skipped (no corpus directory)")
        return 0

    files = sorted(CORPUS_DIR.glob("*.c"))
    if not files:
        print("fuzz replay: skipped (empty corpus)")
        return 0

    failed = []
    for f in files:
        ok, reason = replay_one(cccc, f, args.timeout)
        status = "✓" if ok else "✗"
        print(f"  {status} {f.relative_to(REPO_ROOT)} ({reason})")
        if not ok:
            failed.append((f, reason))

    print()
    print(f"fuzz replay: {len(files) - len(failed)}/{len(files)} passed")
    if failed:
        print(f"FAILED: {', '.join(str(f.name) for f, _ in failed)}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
