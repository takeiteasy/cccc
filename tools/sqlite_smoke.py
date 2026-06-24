#!/usr/bin/env python3
"""SQLite amalgamation preprocess smoke-test (regression for ticket #584).

Ticket #584: with -DSQLITE_OS_OTHER=1 the preprocessor intermittently failed to
consume `#define TK_FLOAT 154`, surfacing a parser error ("expected an
identifier, found '154'"). Root cause was a hashmap duplicate-key bug
(fix 602a291): a tombstoned slot could keep a stale macro entry alive past a
later #undef, so command-line -D/-U state did not stick. This test preprocesses
the real SQLite 3.53.2 amalgamation with the exact flags from the ticket repro
and asserts the failure is gone.

The amalgamation zip is large (~2.9MB) and gitignored (`tools/*.zip`), so it is
NOT a tracked `tests/test_*.c`. When the zip is absent (fresh checkout / CI)
this test SKIPs as success and prints the URL + SHA3-256 so it can be fetched.
When present, the zip's SHA3-256 is verified before use.

Exit codes: 0 = pass or skip, 1 = failure (regression detected / setup error).
"""

import hashlib
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

# SQLite 3.53.2 amalgamation. SHA3-256 is the hash published on
# https://sqlite.org/download.html (the PRODUCT lines list SHA3-256, not SHA-256).
SQLITE_VERSION = "3.53.2"
ZIP_NAME = "sqlite-amalgamation-3530200.zip"  # pinned: do NOT glob (a stale 3.47.0 zip also lives in tools/)
ZIP_SHA3_256 = "81142986038e18f96c4a54e1a72562ae17e502a916f2a7701eff43388cbf1a40"
ZIP_URL = "https://sqlite.org/2026/sqlite-amalgamation-3530200.zip"
MEMBER = "sqlite-amalgamation-3530200/sqlite3.c"

# Exact flags from the #584 repro. -DSQLITE_OS_OTHER=1 is the trigger; the two
# -U flags exercise the command-line undef path that the hashmap bug corrupted.
SQLITE_FLAGS = [
    "-DSQLITE_OMIT_LOAD_EXTENSION",
    "-DSQLITE_THREADSAFE=0",
    "-DSQLITE_DISABLE_INTRINSIC",
    "-DSQLITE_OS_OTHER=1",
    "-U__APPLE__",
    "-U__MACH__",
]


def sha3_256(path: Path) -> str:
    h = hashlib.sha3_256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    script_dir = Path(__file__).parent.parent.resolve()
    cccc = script_dir / "cccc"
    include = script_dir / "include"
    zip_path = script_dir / "tools" / ZIP_NAME

    print(f"SQLite {SQLITE_VERSION} preprocess smoke-test (#584 regression)")

    if not cccc.exists():
        print(f"  ✗ {cccc.name} not found — run 'make' first.")
        return 1

    if not zip_path.exists():
        print(f"  ⊘ SKIP: {zip_path.relative_to(script_dir)} not present.")
        print(f"    Fetch it to enable this test:")
        print(f"      curl -fsSL -o tools/{ZIP_NAME} {ZIP_URL}")
        print(f"    Expected SHA3-256: {ZIP_SHA3_256}")
        return 0

    actual = sha3_256(zip_path)
    if actual != ZIP_SHA3_256:
        print(f"  ✗ {ZIP_NAME} SHA3-256 mismatch:")
        print(f"    expected {ZIP_SHA3_256}")
        print(f"    got      {actual}")
        print(f"    Re-download from {ZIP_URL}")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(zip_path) as zf:
            zf.extract(MEMBER, tmp)
        src = Path(tmp) / MEMBER

        cmd = [str(cccc), "-E", "-I", str(include), *SQLITE_FLAGS, str(src)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        out, err = result.stdout, result.stderr

        # 1. Preprocessing must succeed.
        if result.returncode != 0:
            print(f"  ✗ preprocess exited {result.returncode} (expected 0)")
            for line in err.splitlines()[:5]:
                print(f"    {line}")
            return 1

        # 2. The #584 symptom: the `#define TK_FLOAT 154` name being expanded /
        #    the directive reaching the parser as raw tokens.
        if "expected an identifier" in err or "TK_FLOAT" in err:
            print("  ✗ #584 regression: parser saw the #define directive line")
            for line in err.splitlines()[:5]:
                print(f"    {line}")
            return 1

        # 3. Positive assertion — guard against a flag silently failing to apply
        #    and the run "passing" for the wrong reason (e.g. empty output).
        #    A full preprocess of sqlite3.c is tens of thousands of lines and
        #    must still contain core API identifiers.
        n_lines = out.count("\n")
        if n_lines < 50000 or "sqlite3_step" not in out:
            print(f"  ✗ preprocess output looks truncated "
                  f"({n_lines} lines, sqlite3_step present={'sqlite3_step' in out})")
            return 1

        print(f"  ✓ preprocessed cleanly ({n_lines} lines), no TK_FLOAT error")
        return 0


if __name__ == "__main__":
    sys.exit(main())
