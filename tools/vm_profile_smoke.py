#!/usr/bin/env python3
"""--vm-profile smoke tests.

Cases:
  1. Source mode prints a comptime section and the run section.
  2. An atexit handler does not reset the run profile: main's loop is still
     counted.
  3. -c=generated and -c=native print the comptime section, and the native
     binary still runs.
  4. -m --json leaves stdout as the C dump and writes the profile JSON, with
     a "comptime" object and no run keys, to stderr.
  5. Source mode --json writes run keys at the top level plus "comptime".

Exit codes: 0 = all cases pass, 1 = any failure.
"""

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

COMPTIME_SRC = """\
[[cccc::comptime]]
Node *ct_add(Node *a, Node *b) {
    return MakeBinary(NK_ADD, a, b);
}

int main(void) {
    int v = ct_add(20, 22);
    return v == 42 ? 42 : 1;
}
"""

ATEXIT_SRC = """\
#include <stdlib.h>
static void handler(void) {}
int main(void) {
    atexit(handler);
    int s = 0;
    for (int i = 0; i < 1000; i++)
        s += i;
    return s ? 42 : 1;
}
"""


def run(cmd):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=120)


def section_total(stderr, title):
    m = re.search(rf"^{re.escape(title)}\ntotal_opcodes: (\d+)$", stderr, re.M)
    return int(m.group(1)) if m else None


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", help="cccc binary (default: ./cccc)")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parent.parent
    cccc = Path(args.binary).resolve() if args.binary else root / "cccc"

    print("--vm-profile smoke tests")
    if not cccc.exists():
        print(f"  FAIL: {cccc} not found -- run 'make' first.")
        return 1

    failures = []

    def check(name, ok, detail=""):
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)
            if detail:
                print("    " + detail.strip().replace("\n", "\n    ")[-1500:])

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        src = tmp / "comptime.c"
        src.write_text(COMPTIME_SRC)
        atexit_src = tmp / "atexit.c"
        atexit_src.write_text(ATEXIT_SRC)

        r = run([cccc, "--vm-profile", src])
        ct = section_total(r.stderr, "VM comptime opcode profile")
        rt = section_total(r.stderr, "VM opcode profile")
        check("source mode reports comptime and run sections",
              r.returncode == 42 and ct and rt, r.stderr)

        r = run([cccc, "--vm-profile", atexit_src])
        rt = section_total(r.stderr, "VM opcode profile")
        check("atexit handler keeps main's counts",
              r.returncode == 42 and rt is not None and rt >= 1000
              and "cycles:" not in r.stderr, r.stderr)

        r = run([cccc, "--vm-profile", "-c=generated", "-o", tmp / "out.gen.c", src])
        ct = section_total(r.stderr, "VM comptime opcode profile")
        check("-c=generated reports comptime",
              r.returncode == 0 and ct and "VM opcode profile\n" not in r.stderr, r.stderr)

        exe = tmp / "native"
        r = run([cccc, "--vm-profile", "-c=native", "-o", exe, src])
        ct = section_total(r.stderr, "VM comptime opcode profile")
        ran = run([exe]).returncode if r.returncode == 0 else None
        check("-c=native reports comptime", r.returncode == 0 and ct and ran == 42, r.stderr)

        r = run([cccc, "--vm-profile", "--json", "-m", src])
        try:
            prof = json.loads(r.stderr)
        except json.JSONDecodeError:
            prof = None
        check("-m --json keeps stdout as C and profile JSON on stderr",
              r.returncode == 0 and "int main" in r.stdout and '"tool"' not in r.stdout
              and prof is not None and prof.get("mode") == "expand"
              and prof.get("comptime", {}).get("total_opcodes", 0) > 0
              and "total_opcodes" not in prof,
              r.stderr)

        r = run([cccc, "--vm-profile", "--json", src])
        try:
            prof = json.loads(r.stdout[r.stdout.find("{"):])
        except json.JSONDecodeError:
            prof = None
        check("source --json has run keys and a comptime object",
              prof is not None and prof.get("mode") == "source"
              and prof.get("total_opcodes", 0) > 0
              and prof.get("comptime", {}).get("total_opcodes", 0) > 0, r.stdout[-1500:])

    if failures:
        print(f"  {len(failures)} case(s) failed")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
