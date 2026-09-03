#!/usr/bin/env python3
"""Header resolution smoke tests (#891 regression).

`tools/tests.py` always invokes the built `cccc` from the repo root with an
explicit `-I./include` (see tools/testing/runner.py), so the existing suite
structurally cannot catch a regression in resolving CCCC's own bundled
headers from *any* process CWD with *no* include flags -- which is exactly
what #891 reported broken. This script runs the built `cccc` from a fresh
temp directory instead, with no `-I`/`-i` flags unless a case says otherwise.

Cases:
  1. `#include <stdbool.h>` with zero flags (the reported failure).
  2. A `[[cccc::comptime]]` program, exercising `<implicit-reflection.h>`'s
     own `#include <stdbool.h>` (macros.c's implicit_reflection_tokens).
  3. `#include <stdio.h>` with zero flags (the reported failure).
  4. `stdbool.h` under `--no-builtin-includes --use-system-headers`: an
     *owned* header (VM-ABI-coupled; see is_compiler_owned_header in
     preprocess.c) must still resolve even when --no-builtin-includes asks
     non-owned headers to fail rather than fall back.
  5. `-c=native` with a real `#include <stdio.h>`: must compile *and*
     produce real stdout (the second half of #891 -- `typedef void FILE;`
     colliding with the real system stdio.h's `struct __sFILE`).
  6. `-c=native` with a primary file plus a sibling quoted project header
     (`#include "local.h"`): exercises the `-I<primary file's dirname>`
     forwarded to the native compiler, since cccc auto-captures and
     re-emits the quoted #include into a temp-directory source file.
  7. Static audit (#1070): a bundled header with its own `#include_next`
     hand-off (stdint.h/stdarg.h/errno.h/fenv.h/getopt.h/stdio.h/cdefs.h)
     must never be *quoted*-included from another bundled header. Under a
     real host compiler reading these via `-I./include`, a quoted include
     resolves by the "same directory as the including file" rule; real GCC
     (confirmed: 13.3.0) then resumes its own #include_next search from
     position 0 of the -I list rather than from where the quoted include
     actually resolved, looping back to CCCC's own copy instead of handing
     off to the real system header -- the include guard silently no-ops it
     and nothing gets defined. Angle-bracket sidesteps GCC's ambiguity
     entirely; this case can't reproduce on a clang-only host (clang
     resolves it correctly), so it has to be a static check, not a
     round-trip.
  8-10. `<sys/stat.h>`/`<sys/time.h>`/`<sys/times.h>` with zero flags (#1194):
     each of these bundled headers itself quote-includes "../time.h" --
     a spelling that can only resolve relative to the embedded header's own
     virtual "<embedded>/..." path, not by literal table-key lookup, so this
     is a distinct failure mode from cases 1/3 above (which never nest a
     relative include at all).
  11. `-c=native` with `<sys/stat.h>`, zero flags: the same #1194 class, but
     through the native round-trip -- cccc's own resolution of the nested
     "../time.h" happens the same way regardless of backend, but this
     confirms the fix reaches -c=native's compile step too, not just the VM.
  12. `-c=native` with `<locale.h>` + `<xlocale.h>` (#1275): CCCC bundles a
     trimmed `struct lconv` in locale.h and, before #1275, had no bundled
     xlocale.h at all -- a bare `#include <xlocale.h>` fell through to the
     real host's own copy, whose transitively-included `_locale.h`
     redeclares `struct lconv` incompatibly with CCCC's. Runs on both hosts:
     on macOS it exercises the collision directly; on Linux (where glibc
     dropped its own `<xlocale.h>` in 2.26) it exercises
     serialize_program.c's replay filter that drops the captured
     `#include <xlocale.h>` line rather than replaying it to a host that no
     longer has the file.
  13. Static audit, generalizing #1275: every `#include <...>` in cccc's own
     `src/*.c`/`src/*.h`/`src/stdlib/*`/`include/cccc/*.h` must either
     resolve under `include/` or appear in an explicit allowlist below, so a
     future unbundled system header collision gets caught at edit time
     rather than at #1132's ~10-minute-per-run self-hosting spike scale.

Exit codes: 0 = all cases pass, 1 = any failure.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path


# #1201: every subprocess this script spawns must go through run() below --
# a direct subprocess.run() call bypasses the stdin/timeout guard entirely
# and reopens the exact wedge class #1201 fixed. When adding a new case,
# route it through run() rather than calling subprocess.run directly.
def run(cmd, cwd=None):
    # #1202: this local helper used to inherit the harness's own stdin with
    # no timeout -- the same class of hazard #1201 fixed in the sibling
    # host_attribute_link_smoke.py and tools/testing/proc.py's run_capture()
    # (#1185) chokepoint fixes for the main per-test-file suite, which this
    # standalone script bypasses since it runs in-process via importlib, not
    # as a per-test spawn.
    try:
        return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd,
                               stdin=subprocess.DEVNULL, timeout=120)
    except subprocess.TimeoutExpired as e:
        return subprocess.CompletedProcess(cmd, 124, "", f"timed out: {e}")


def write(path: Path, contents: str):
    path.write_text(contents)


def case_stdbool(cccc: Path, tmp: str) -> bool:
    print("  1: #include <stdbool.h>, zero flags")
    src = Path(tmp) / "stdbool_case.c"
    write(src, "#include <stdbool.h>\nint main(void){bool b=true;return b?42:0;}\n")
    result = run([str(cccc), src.name], cwd=tmp)
    if result.returncode != 42:
        print(f"    FAIL: exit {result.returncode}\n    {result.stderr}")
        return False
    print("    ok")
    return True


def case_comptime(cccc: Path, tmp: str) -> bool:
    print("  2: [[cccc::comptime]] program (<implicit-reflection.h>), zero flags")
    src = Path(tmp) / "comptime_case.c"
    write(src, (
        "[[cccc::comptime]] Node *five(void){ return MakeIntLiteral(42); }\n"
        "int main(void){ return five(); }\n"
    ))
    result = run([str(cccc), src.name], cwd=tmp)
    if result.returncode != 42:
        print(f"    FAIL: exit {result.returncode}\n    {result.stderr}")
        return False
    print("    ok")
    return True


def case_stdio(cccc: Path, tmp: str) -> bool:
    print("  3: #include <stdio.h>, zero flags")
    src = Path(tmp) / "stdio_case.c"
    write(src, '#include <stdio.h>\nint main(void){printf("hi\\n");return 42;}\n')
    result = run([str(cccc), src.name], cwd=tmp)
    if result.returncode != 42 or "hi" not in result.stdout:
        print(f"    FAIL: exit {result.returncode}, stdout={result.stdout!r}\n    {result.stderr}")
        return False
    print("    ok")
    return True


def _case_relative_include_header(number: int, header: str):
    def case(cccc: Path, tmp: str) -> bool:
        print(f"  {number}: #include <{header}>, zero flags "
              f"(own \"../time.h\" quote-include, #1194)")
        src = Path(tmp) / (header.replace("/", "_").replace(".", "_") + "_case.c")
        write(src, f"#include <{header}>\nint main(void){{return 42;}}\n")
        result = run([str(cccc), src.name], cwd=tmp)
        if result.returncode != 42:
            print(f"    FAIL: exit {result.returncode}\n    {result.stderr}")
            return False
        print("    ok")
        return True
    return case


case_sys_stat = _case_relative_include_header(8, "sys/stat.h")
case_sys_time = _case_relative_include_header(9, "sys/time.h")
case_sys_times = _case_relative_include_header(10, "sys/times.h")


def case_native_sys_stat(cccc: Path, tmp: str) -> bool:
    print("  11: -c=native with #include <sys/stat.h>, zero flags (#1194)")
    src = Path(tmp) / "native_sys_stat.c"
    out = Path(tmp) / "native_sys_stat_out"
    write(src, "#include <sys/stat.h>\nint main(void){return 42;}\n")
    result = run([str(cccc), "-c=native", "-o", out.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: exit {run_result.returncode}")
        return False
    print("    ok")
    return True


def case_no_builtin_includes_owned(cccc: Path, tmp: str) -> bool:
    print("  4: stdbool.h under --no-builtin-includes --use-system-headers (owned header)")
    src = Path(tmp) / "owned_case.c"
    write(src, "#include <stdbool.h>\nint main(void){bool b=true;return b?42:0;}\n")
    result = run(
        [str(cccc), "--no-builtin-includes", "--use-system-headers",
         "--sysroot", sysroot(), src.name],
        cwd=tmp,
    )
    if result.returncode != 42:
        print(f"    FAIL: exit {result.returncode}\n    {result.stderr}")
        return False
    print("    ok")
    return True


def case_native_stdio(cccc: Path, tmp: str) -> bool:
    print("  5: -c=native with real #include <stdio.h> (FILE/fpos_t collision)")
    src = Path(tmp) / "native_stdio.c"
    out = Path(tmp) / "native_stdio_out"
    write(src, '#include <stdio.h>\nint main(void){printf("hi\\n");return 42;}\n')
    result = run([str(cccc), "-c=native", "-o", out.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42 or "hi" not in run_result.stdout:
        print(f"    FAIL: exit {run_result.returncode}, stdout={run_result.stdout!r}")
        return False
    print("    ok")
    return True


def case_native_quoted_include(cccc: Path, tmp: str) -> bool:
    print("  6: -c=native with a sibling quoted project header")
    local_h = Path(tmp) / "native_local.h"
    write(local_h, "struct point { int x; int y; };\n")
    src = Path(tmp) / "native_quoted.c"
    out = Path(tmp) / "native_quoted_out"
    write(src, (
        '#include "native_local.h"\n'
        '#include <stdio.h>\n'
        'int main(void){\n'
        '    struct point p = {1,2};\n'
        '    printf("%d %d\\n", p.x, p.y);\n'
        '    return 42;\n'
        '}\n'
    ))
    result = run([str(cccc), "-c=native", "-o", out.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42 or "1 2" not in run_result.stdout:
        print(f"    FAIL: exit {run_result.returncode}, stdout={run_result.stdout!r}")
        return False
    print("    ok")
    return True


def case_owned_header_include_form(cccc: Path, tmp: str) -> bool:
    print("  7: no bundled header quote-includes an #include_next header (#1070)")
    root = Path(__file__).parent.parent
    include_dir = root / "include"
    # Anchored at line-start (allowing only leading whitespace) so a comment
    # merely *mentioning* "#include_next" (as several of these headers' own
    # explanatory comments do) doesn't get mistaken for the real directive.
    handoff_re = re.compile(r'^\s*#\s*include_next\b', re.MULTILINE)
    quote_re = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
    handoff_names = set()
    for h in include_dir.rglob("*.h"):
        if handoff_re.search(h.read_text(errors="ignore")):
            handoff_names.add(h.name)
    if not handoff_names:
        print("    FAIL: no #include_next headers found -- audit is stale")
        return False
    bad = []
    for h in include_dir.rglob("*.h"):
        for m in quote_re.finditer(h.read_text(errors="ignore")):
            name = Path(m.group(1)).name
            if name in handoff_names:
                bad.append(f"{h.relative_to(root)}: quotes \"{m.group(1)}\"")
    if bad:
        print("    FAIL: quoted include(s) of an #include_next header:")
        for line in bad:
            print(f"      {line}")
        return False
    print("    ok")
    return True


def case_native_xlocale(cccc: Path, tmp: str) -> bool:
    print("  12: -c=native with #include <locale.h> + <xlocale.h> (#1275)")
    src = Path(tmp) / "native_xlocale.c"
    out = Path(tmp) / "native_xlocale_out"
    write(src, "#include <locale.h>\n#include <xlocale.h>\nint main(void){return 42;}\n")
    result = run([str(cccc), "-c=native", "-o", out.name, src.name], cwd=tmp)
    if result.returncode != 0:
        print(f"    FAIL: compile exited {result.returncode}\n    {result.stderr}")
        return False
    run_result = run([f"./{out.name}"], cwd=tmp)
    if run_result.returncode != 42:
        print(f"    FAIL: exit {run_result.returncode}")
        return False
    print("    ok")
    return True


# #1275: system headers cccc's own source reaches for via a bare
# `#include <...>` that are NOT bundled under include/ -- each entry needs a
# one-line reason this is safe (Windows/Linux-only source, or a library with
# no CCCC-guest-visible ABI surface at all, unlike locale.h/xlocale.h/etc).
# A name reaching this allowlist without a reason is exactly the #1275 bug
# class: an unbundled header a real host resolves on its own, silently ready
# to collide with a same-named bundled header elsewhere in the same TU.
UNBUNDLED_SYSTEM_HEADER_ALLOWLIST = {
    "ffi.h":                "libffi's own public header, no bundled copy",
    "curl/curl.h":           "optional CCCC_HAS_CURL dependency, no bundled copy",
    "readline/readline.h":  "optional line-editing dependency, no bundled copy",
    "readline/history.h":   "optional line-editing dependency, no bundled copy",
    "features.h":           "glibc-only feature-test header, Linux source paths only",
    "sys/vfs.h":             "Linux-only struct statfs location, no macOS bundled copy",
    "io.h":                  "Windows-only, no POSIX bundled copy",
    "windows.h":             "Windows-only, no POSIX bundled copy",
    "synchapi.h":            "Windows-only, no POSIX bundled copy",
    "processthreadsapi.h":  "Windows-only, no POSIX bundled copy",
}


def case_unbundled_header_audit(cccc: Path, tmp: str) -> bool:
    print("  13: every #include <...> in cccc's own source is bundled or allowlisted (#1275)")
    root = Path(__file__).parent.parent
    src_dirs = [root / "src", root / "include" / "cccc"]
    include_dir = root / "include"
    angle_re = re.compile(r'^\s*#\s*include\s*<([^>]+)>', re.MULTILINE)
    bad = []
    for d in src_dirs:
        for f in d.rglob("*"):
            if f.suffix not in (".c", ".h") or f.name in ("std.c",):
                continue
            # src/backtrace is a vendored libbacktrace copy, built as its
            # own separate library (Makefile's build/lib/libbacktrace) and
            # never part of the self-hosting spike's `src/*.c` file set
            # (a bare glob, not recursive) -- its own platform-probing
            # system includes (link.h, mach-o/dyld.h, ...) are out of this
            # audit's scope, the same way they're out of #1132's.
            if "backtrace" in f.relative_to(root).parts:
                continue
            for m in angle_re.finditer(f.read_text(errors="ignore")):
                name = m.group(1)
                if (include_dir / name).exists():
                    continue
                if name in UNBUNDLED_SYSTEM_HEADER_ALLOWLIST:
                    continue
                bad.append(f"{f.relative_to(root)}: #include <{name}>")
    if bad:
        print("    FAIL: unbundled, unallowlisted system header include(s):")
        for line in bad:
            print(f"      {line}")
        return False
    print("    ok")
    return True


def sysroot() -> str:
    # xcrun is macOS-only; on any other platform (e.g. the Linux CI
    # container) it doesn't exist at all, and subprocess.run() raises
    # FileNotFoundError rather than giving a non-zero returncode to check --
    # go straight to the "/" fallback instead of letting that propagate.
    if sys.platform != "darwin":
        return "/"
    result = run(["xcrun", "--show-sdk-path"])
    if result.returncode == 0 and result.stdout.strip():
        return result.stdout.strip()
    return "/"


def main() -> int:
    root = Path(__file__).parent.parent.resolve()
    cccc = root / "cccc"

    print("Header resolution smoke tests (#891)")

    if not cccc.exists():
        print(f"  FAIL: {cccc.name} not found — run 'make' first.")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        cases = [
            case_stdbool,
            case_comptime,
            case_stdio,
            case_no_builtin_includes_owned,
            case_native_stdio,
            case_native_quoted_include,
            case_owned_header_include_form,
            case_sys_stat,
            case_sys_time,
            case_sys_times,
            case_native_sys_stat,
            case_native_xlocale,
            case_unbundled_header_audit,
        ]
        results = [case(cccc, tmp) for case in cases]

    if all(results):
        print(f"All {len(results)} header resolution smoke cases passed.")
        return 0
    print(f"{results.count(False)} of {len(results)} header resolution smoke cases FAILED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
