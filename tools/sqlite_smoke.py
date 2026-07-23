#!/usr/bin/env python3
"""SQLite 3.53.2 amalgamation smoke tests.

Two phases, both conditional on the zip being present:

  Phase 1 — preprocess (#584 regression)
    Preprocesses sqlite3.c with -DSQLITE_OS_OTHER=1 and command-line -U flags,
    asserting the TK_FLOAT parse error is gone (hashmap duplicate-key fix).

  Phase 2 — compile + run (#587/#588/#624 regression + functional smoke)
    Compiles sqlite3.c with --no-comptime (-C) to skip the comptime/macro phase
    entirely (sqlite doesn't use [[cccc::comptime]] so this is the right fix for
    large third-party TUs).  Together with a minimal driver, runs the result and
    expects exit code 42.  Exercises: CREATE TABLE, INSERT, prepared SELECT,
    column accessors.

The zip (~2.9 MB) is gitignored (tools/*.zip).  When absent both phases skip
gracefully and print the fetch URL + SHA3-256.  When present, SHA3-256 is
verified before use.

Exit codes: 0 = pass or skip, 1 = failure.
"""

import hashlib
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

SQLITE_VERSION = "3.53.2"
ZIP_NAME = "sqlite-amalgamation-3530200.zip"
ZIP_SHA3_256 = "81142986038e18f96c4a54e1a72562ae17e502a916f2a7701eff43388cbf1a40"
ZIP_URL = "https://sqlite.org/2026/sqlite-amalgamation-3530200.zip"

# Compile flags used for both phases (where applicable)
COMPILE_FLAGS = [
    "-DSQLITE_OMIT_LOAD_EXTENSION",
    "-DSQLITE_DISABLE_INTRINSIC",
]

# Driver source: open :memory:, run a simple query, return 42 on success.
DRIVER_C = """\
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) { fprintf(stderr, "open: %s\\n", sqlite3_errmsg(db)); return 1; }

    rc = sqlite3_exec(db, "CREATE TABLE t(x INTEGER, y TEXT);", NULL, NULL, NULL);
    if (rc != SQLITE_OK) { fprintf(stderr, "create: %s\\n", sqlite3_errmsg(db)); sqlite3_close(db); return 1; }

    rc = sqlite3_exec(db, "INSERT INTO t VALUES(1,'hello'),(2,'world');", NULL, NULL, NULL);
    if (rc != SQLITE_OK) { fprintf(stderr, "insert: %s\\n", sqlite3_errmsg(db)); sqlite3_close(db); return 1; }

    rc = sqlite3_prepare_v2(db, "SELECT x, y FROM t ORDER BY x;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) { fprintf(stderr, "prepare: %s\\n", sqlite3_errmsg(db)); sqlite3_close(db); return 1; }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int x = sqlite3_column_int(stmt, 0);
        const char *y = (const char *)sqlite3_column_text(stmt, 1);
        if ((count == 0 && (x != 1 || __builtin_strcmp(y, "hello") != 0)) ||
            (count == 1 && (x != 2 || __builtin_strcmp(y, "world") != 0))) {
            fprintf(stderr, "row %d wrong: %d %s\\n", count, x, y);
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return 1;
        }
        count++;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (count != 2) { fprintf(stderr, "expected 2 rows, got %d\\n", count); return 1; }
    return 42;
}
"""


def sha3_256(path: Path) -> str:
    h = hashlib.sha3_256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run(cmd: list, **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, **kwargs)


def phase1_preprocess(cccc: Path, include: Path, src: Path) -> bool:
    """#584 regression: preprocess with -DSQLITE_OS_OTHER=1 and -U flags."""
    print("  Phase 1: preprocess (#584 regression)")
    flags = [
        "-DSQLITE_OMIT_LOAD_EXTENSION",
        "-DSQLITE_DISABLE_INTRINSIC",
        "-DSQLITE_OS_OTHER=1",
        "-U__APPLE__",
        "-U__MACH__",
    ]
    result = run([str(cccc), "-E", "-I", str(include), *flags, str(src)])
    out, err = result.stdout, result.stderr

    if result.returncode != 0:
        print(f"    FAIL: preprocess exited {result.returncode}")
        for line in err.splitlines()[:5]:
            print(f"    {line}")
        return False

    if "expected an identifier" in err or "TK_FLOAT" in err:
        print("    FAIL: #584 regression — TK_FLOAT error returned")
        for line in err.splitlines()[:5]:
            print(f"    {line}")
        return False

    n_lines = out.count("\n")
    if n_lines < 50000 or "sqlite3_step" not in out:
        print(f"    FAIL: output looks truncated ({n_lines} lines, "
              f"sqlite3_step={'yes' if 'sqlite3_step' in out else 'no'})")
        return False

    print(f"    ok  preprocessed cleanly ({n_lines} lines)")
    return True


def phase2_compile_run(cccc: Path, include: Path, src: Path, tmp: str) -> bool:
    """#587/#588/#624 regression + functional smoke: compile and run a simple query."""
    print("  Phase 2: compile + run (#587/#588/#624 + functional)")

    driver = Path(tmp) / "driver.c"
    driver.write_text(DRIVER_C)

    cmd = [
        str(cccc),
        "-I", str(include),
        "-I", str(src.parent),  # sqlite3.h lives next to sqlite3.c
        "-U__APPLE__",
        "-U__MACH__",
        "--no-comptime",        # sqlite doesn't use [[cccc::comptime]]; skip the
                                # comptime/macro phase entirely (#624)
        *COMPILE_FLAGS,
        str(src),
        str(driver),
    ]
    result = run(cmd)
    err = result.stderr

    if "out of temporary registers" in err:
        print("    FAIL: #587 regression — out of temporary registers")
        print("    (deeply nested expression exhausted the codegen temp pool)")
        return False

    if "SIGSEGV" in err or "gen_addr" in err:
        print("    FAIL: #588 regression — SIGSEGV in gen_addr")
        for line in err.splitlines()[:5]:
            print(f"    {line}")
        return False

    if "expected an identifier" in err:
        print("    FAIL: #624 regression — TK_* macro collision (--no-comptime should prevent this)")
        for line in err.splitlines()[:5]:
            print(f"    {line}")
        return False

    if result.returncode != 42:
        print(f"    FAIL: expected exit 42, got {result.returncode}")
        for line in (result.stdout + err).splitlines()[:10]:
            print(f"    {line}")
        return False

    print("    ok  compiled and ran; query returned 2 rows, exit 42")
    return True


def main() -> int:
    root = Path(__file__).parent.parent.resolve()
    cccc = root / "cccc"
    include = root / "include"
    zip_path = root / "tools" / ZIP_NAME

    print(f"SQLite {SQLITE_VERSION} smoke tests")

    if not cccc.exists():
        print(f"  FAIL: {cccc.name} not found — run 'make' first.")
        return 1

    if not zip_path.exists():
        print(f"  SKIP: tools/{ZIP_NAME} not present.")
        print(f"    Fetch it to enable these tests:")
        print(f"      curl -fsSL -o tools/{ZIP_NAME} {ZIP_URL}")
        print(f"    Expected SHA3-256: {ZIP_SHA3_256}")
        return 0

    actual = sha3_256(zip_path)
    if actual != ZIP_SHA3_256:
        print(f"  FAIL: {ZIP_NAME} SHA3-256 mismatch:")
        print(f"    expected {ZIP_SHA3_256}")
        print(f"    got      {actual}")
        print(f"    Re-download from {ZIP_URL}")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(zip_path) as zf:
            zf.extract(f"sqlite-amalgamation-3530200/sqlite3.c", tmp)
            zf.extract(f"sqlite-amalgamation-3530200/sqlite3.h", tmp)
        src = Path(tmp) / "sqlite-amalgamation-3530200" / "sqlite3.c"

        ok1 = phase1_preprocess(cccc, include, src)
        ok2 = phase2_compile_run(cccc, include, src, tmp)

    if ok1 and ok2:
        print("  All phases passed.")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
