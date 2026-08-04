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

Exit codes: 0 = all cases pass, 1 = any failure.
"""

import subprocess
import sys
import tempfile
from pathlib import Path


def run(cmd, cwd):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)


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


def sysroot() -> str:
    result = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True)
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
        ]
        results = [case(cccc, tmp) for case in cases]

    if all(results):
        print(f"All {len(results)} header resolution smoke cases passed.")
        return 0
    print(f"{results.count(False)} of {len(results)} header resolution smoke cases FAILED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
