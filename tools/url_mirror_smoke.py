#!/usr/bin/env python3
"""URL #include mirror smoke tests (#1324).

#1313 rewrote a *captured* URL #include (one whose includer is a
command-line input, or one of CCCC's own bundled/cccc-only headers) to its
resolved on-disk cache path at replay time, so -c=native/-m/-c=generated
output never carries an https:// operand a real host cc can't resolve. #1324
is the gap that left open: a URL #include reached only through an ordinary
*project* header is never auto-captured in the first place (the auto-capture
gate in src/preprocess.c only fires for a command-line input or a
bundled/cccc-only includer), so nothing rewrites it -- the includer's own
captured directive replays that raw URL line verbatim.

The fix mirrors every fetched URL on disk under a URL-shaped path
(<url-cache-dir>/<scheme>:/<host>/<path>, fetch_url_to_cache() in
src/url_fetch.c) and forwards that directory to the host cc as `-idirafter`
(src/main.c), so the raw `#include "https://..."` line resolves as written,
at any depth in the include graph, with no capture and no rewriting needed.

Case 1 below is the load-bearing assumption this whole approach rests on --
that gcc/clang path-join a URL-shaped quoted/angled #include operand against
a search directory the same way any other operand is joined, letting the
doubled '/' after the scheme collapse against a single-'/' on-disk mirror.
It needs no network and no curl-enabled cccc build, so it always runs and
guards against that assumption ever silently breaking on a future toolchain.
Case 2 is the ticket's own end-to-end repro (a URL #include nested inside a
project header) and needs a curl-enabled build; it fetches over the network,
so it skips cleanly otherwise.

Exit codes: 0 = all cases pass (or skipped), 1 = any failure.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(cmd, cwd=None):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd,
                              stdin=subprocess.DEVNULL, timeout=120)
    except subprocess.TimeoutExpired as e:
        return subprocess.CompletedProcess(cmd, 124, "", f"timed out: {e}")


def _find_host_cc():
    for name in ("cc", "clang", "gcc"):
        found = shutil.which(name)
        if found:
            return found
    return None


def case_mirror_path_join(tmp):
    """Case 1: a URL-shaped mirror tree resolves through -idirafter on a
    real host cc, no network/curl needed -- pins the path-join assumption
    the whole #1324 fix rests on."""
    print("  1: host cc resolves a URL-shaped #include via -idirafter")
    cc = _find_host_cc()
    if not cc:
        print("    skipped (no host cc/clang/gcc on PATH)")
        return True

    cache_dir = Path(tmp) / "url-cache"
    mirror = cache_dir / "https:" / "h" / "p"
    mirror.mkdir(parents=True)
    (mirror / "x.h").write_text("int ok_marker;\n")

    ok = True
    for spelling in ('#include "https://h/p/x.h"', "#include <https://h/p/x.h>"):
        src = Path(tmp) / "t.c"
        src.write_text(f"{spelling}\nint main(void) {{ int x = ok_marker; return x - x + 42; }}\n")
        obj = Path(tmp) / "t.o"
        result = run([cc, "-idirafter", str(cache_dir), "-c", str(src), "-o", str(obj)])
        if result.returncode != 0:
            print(f"    FAIL: {spelling!r} did not resolve\n"
                  f"    stderr={result.stderr!r}")
            ok = False
    if ok:
        print("    ok")
    return ok


def case_nested_url_include_native(tmp, cccc):
    """Case 2: the ticket's own repro -- a URL #include reached only
    through a project header, compiled via -c=native. Needs a curl-enabled
    build and network access; skips cleanly otherwise."""
    print("  2: URL #include nested in a project header, via -c=native")
    check = run([str(cccc), "--version"])
    if "curl" not in (check.stdout + check.stderr).lower():
        # --version's feature list doesn't name curl on a non-curl build;
        # cheaper than parsing it precisely is just trying the compile and
        # treating "URL includes require CCCC to be built with
        # CCCC_HAS_CURL=1" as a clean skip below.
        pass

    src_dir = Path(tmp) / "nested"
    src_dir.mkdir()
    (src_dir / "myheader.h").write_text(
        '#include "https://raw.githubusercontent.com/nothings/stb/master/stb_sprintf.h"\n')
    (src_dir / "main.c").write_text(
        '#include "myheader.h"\n'
        'int main(void) { return STB_SPRINTF_MIN == 512 ? 42 : 1; }\n')
    out = src_dir / "a.out"
    result = run([str(cccc), "-c=native", "main.c", "-o", str(out)], cwd=src_dir)
    if "CCCC_HAS_CURL" in result.stderr:
        print("    skipped (cccc built without libcurl)")
        return True
    if result.returncode != 0:
        # A network hiccup (offline CI, DNS failure, etc.) is not this
        # fix's concern -- only fail on something that looks like our own
        # code, not a fetch failure.
        if "failed to fetch URL" in result.stderr:
            print(f"    skipped (URL fetch failed, likely offline): "
                  f"{result.stderr.strip()!r}")
            return True
        print(f"    FAIL: -c=native compile failed\n    stderr={result.stderr!r}")
        return False
    run_result = run([str(out)])
    if run_result.returncode != 42:
        print(f"    FAIL: exit {run_result.returncode}, expected 42")
        return False
    print("    ok")
    return True


def main():
    root = Path(__file__).parent.parent.resolve()
    cccc = root / "cccc"

    print("URL #include mirror smoke tests (#1324)")

    with tempfile.TemporaryDirectory() as tmp:
        results = [case_mirror_path_join(tmp)]
        if cccc.exists():
            with tempfile.TemporaryDirectory() as tmp2:
                results.append(case_nested_url_include_native(tmp2, cccc))
        else:
            print("  2: skipped (cccc not found -- run 'make' first)")

    if all(results):
        print(f"All {len(results)} URL mirror smoke cases passed (or skipped).")
        return 0
    print(f"{results.count(False)} of {len(results)} URL mirror smoke cases FAILED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
