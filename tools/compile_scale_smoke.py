#!/usr/bin/env python3
"""Compile-time scaling smoke test.

Compiles a generated translation unit with N and then 4N functions and fails
if the time grows superlinearly. Linear work grows about 4x; a scan that is
quadratic in the number of globals grows about 16x. Comparing the two sizes,
rather than checking an absolute time, keeps the check independent of machine
speed.

Each function adds a file-scope static, locals and a call, so name lookup,
-Wshadow and -Wunused all see a large file scope.

Exit codes: 0 = linear enough, 1 = failure.
"""

import argparse
import subprocess
import sys
import tempfile
import time
from pathlib import Path

BASE_N = 3000
SCALE = 4
MAX_RATIO = 10.0
REPS = 3


def generate(n):
    out = ["struct pt { int x, y; };"]
    for i in range(n):
        out.append(f"static int g{i} = {i};")
        out.append(f"""static int f{i}(struct pt *p, int n) {{
    int acc = g{i};
    for (int k = 0; k < n; k++) {{
        int x = p[k].x * {i % 7 + 1};
        acc += x - (p[k].y >> 1);
    }}
    switch (acc & 3) {{ case 0: return acc; case 1: return acc + 1; default: return -acc; }}
}}""")
    out.append(f"static int unused_tail_{n}(void) {{ return 0; }}")
    out.append("int main(void) {\n    struct pt p[1] = {{1, 2}};\n    int s = 0;")
    out += [f"    s += f{i}(p, 1);" for i in range(n)]
    out.append("    return s ? 42 : 42;\n}")
    return "\n".join(out) + "\n"


def best_time(cccc, src):
    best = None
    for _ in range(REPS):
        start = time.perf_counter()
        r = subprocess.run([str(cccc), "-c=generated", "-Wall", "-Wshadow", str(src), "-o", "/dev/null"],
                           capture_output=True, text=True, timeout=600)
        elapsed = time.perf_counter() - start
        if r.returncode != 0:
            raise RuntimeError(f"{src.name} failed to compile:\n{r.stderr[-2000:]}")
        best = elapsed if best is None else min(best, elapsed)
    return best


def measure(cccc, small, large):
    t_small = best_time(cccc, small)
    t_large = best_time(cccc, large)
    return t_small, t_large, t_large / t_small


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", help="cccc binary (default: ./cccc)")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parent.parent
    cccc = Path(args.binary).resolve() if args.binary else root / "cccc"

    print("Compile-time scaling smoke test")
    if not cccc.exists():
        print(f"  FAIL: {cccc} not found -- run 'make' first.")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        small, large = Path(tmp) / "small.c", Path(tmp) / "large.c"
        small.write_text(generate(BASE_N))
        large.write_text(generate(BASE_N * SCALE))
        try:
            # A shared CI runner can slow one run; only a ratio that stays
            # over the limit on a second measurement is a failure.
            for attempt in range(2):
                t_small, t_large, ratio = measure(cccc, small, large)
                print(f"  {BASE_N} functions: {t_small:.3f}s, {BASE_N * SCALE}: {t_large:.3f}s, "
                      f"ratio {ratio:.1f}x (limit {MAX_RATIO:.0f}x)")
                if ratio <= MAX_RATIO:
                    break
        except (RuntimeError, subprocess.TimeoutExpired) as e:
            print(f"  FAIL: {e}")
            return 1

    if ratio > MAX_RATIO:
        print("  FAIL: compile time grows superlinearly with the number of globals")
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
