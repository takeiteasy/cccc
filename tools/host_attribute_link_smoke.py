#!/usr/bin/env python3
"""Host `__attribute__`-stripping duplicate-symbol link smoke test (#1199).

`src/internal.h` used to unconditionally `#define __attribute__(x)` (guarded
by an `#ifndef __attribute__` that never fired -- `__attribute__` is a
keyword under the GNU family, never a predefined macro). Under a *real* gcc
on Darwin that deleted the `__gnu_inline__` out of `<_ctype.h>`'s
`__header_inline` (`extern __inline __attribute__((__gnu_inline__))`),
leaving a bare `extern __inline` -- a C99 external definition under
`-std=c23` -- so every translation unit reaching `<ctype.h>`/`<wctype.h>`
emitted a full set of ctype/wctype helpers, and linking two or more such TUs
together failed with duplicate symbols. This is exactly what broke
`--build`'s own bootstrap under `CCCC_BUILD_CC=<real gcc>` (53 duplicate
symbols, e.g. `___toupper_l`, across every `posix_*.o`).

Clang was never affected -- its `__header_inline` resolves to plain
`inline`, so there was nothing for the strip to break -- so this can only be
exercised under a genuine gcc, not the `cc`/`gcc` symlink-to-clang macOS
ships. When no real (non-clang) gcc is available on this host, every case is
skipped rather than failed.

**Platform coverage note:** this reproduces on macOS only. glibc's own
ctype/wctype functions (`toupper`, `iswalpha`, etc.) are not extern-inline
in the same way -- verified against real gcc 13.3.0 on Ubuntu 24.04
(`cccc-linux-amd64` container): forcing the old unconditional strip back on
still leaves them as plain undefined references (`U`) at both `-O0` and
`-O2`, never externally defined. (glibc *does* have this class of bug for a
different symbol set -- `pthread_equal`/`btowc`/`wctob`/`mbrlen`, per
`build.c`'s `-fgnu89-inline` comment on the Linux branch of
`add_cccc_flags_opt()` -- but that's a distinct repro this test doesn't
cover.) On Linux this suite still runs and still checks a real gcc links a
`<ctype.h>`-using TU cleanly, but it cannot catch a regression of the fix
below -- treat a Linux pass here as a sanity check, not a #1199 regression
guard.

This compiles two translation units that both `#include "internal.h"` (the
same header cccc's own sources pull in) plus `<ctype.h>`/`<wctype.h>`, and
call a couple of the inline helpers -- the same shape that broke `--build`'s
bootstrap -- then links them together and checks both that the link
succeeds and that neither object file carries an externally-visible
ctype/wctype symbol.

Exit codes: 0 = all applicable cases pass (or nothing applicable on this
host), 1 = any failure.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# The specific Darwin/glibc extern-inline ctype/wctype symbols implicated in
# #1199 -- any externally-visible ('T'-class) symbol matching one of these
# names in either object means the strip-based bug (or a regression of the
# fix) reintroduced an external definition.
_WATCHED_SYMBOL_SUBSTRINGS = ("toupper", "tolower", "isalnum", "isalpha",
                              "isblank", "iscntrl", "isdigit", "isgraph",
                              "islower", "isprint", "ispunct", "isspace",
                              "isupper", "isxdigit", "iswalnum", "iswalpha",
                              "iswblank", "iswcntrl", "iswctype", "iswdigit",
                              "iswgraph", "iswlower", "iswprint", "iswpunct",
                              "iswspace", "iswupper", "iswxdigit", "towlower",
                              "towupper", "istype", "wcwidth", "digittoint",
                              "ishexnumber", "isideogram", "isnumber",
                              "isphonogram", "isrune", "isspecial")

# Candidates to probe on PATH, in order, when CCCC_BUILD_CC either isn't set
# or turns out to be clang. Homebrew's macOS gcc formulae install as
# gcc-<major>; a handful of recent majors are tried since this script has no
# way to know which one is installed.
_GCC_CANDIDATES = ["gcc-16", "gcc-15", "gcc-14", "gcc-13", "gcc"]

_SOURCE_TEMPLATE = """\
#include "internal.h"
#include <ctype.h>
#include <wctype.h>

int cccc_attr_link_smoke_{suffix}(int c) {{
    return toupper(c) + iswalpha((wint_t)c);
}}
"""

_MAIN_SOURCE = """\
int cccc_attr_link_smoke_a(int);
int cccc_attr_link_smoke_b(int);
int main(void) {
    return cccc_attr_link_smoke_a(0) + cccc_attr_link_smoke_b(0);
}
"""


def run(cmd, cwd=None):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)


def is_real_gcc(cc: str) -> bool:
    """True if `cc -dM -E -` reports __GNUC__ but not __clang__ (rules out
    macOS's cc/gcc symlinks-to-clang, which would make every case here
    vacuously pass without exercising anything)."""
    found = shutil.which(cc)
    if not found:
        return False
    result = run([found, "-dM", "-E", "-x", "c", "-"])
    if result.returncode != 0:
        return False
    defines = result.stdout
    return "__GNUC__" in defines and "__clang__" not in defines


def find_real_gcc():
    import os
    candidates = []
    env_cc = os.environ.get("CCCC_BUILD_CC")
    if env_cc:
        candidates.append(env_cc)
    candidates.extend(_GCC_CANDIDATES)
    for cc in candidates:
        if is_real_gcc(cc):
            return shutil.which(cc)
    return None


def platform_defines():
    """Mirror build.c's add_cccc_flags_opt() Linux branch: glibc needs
    _DEFAULT_SOURCE/_POSIX_C_SOURCE defined for siginfo_t and friends to be
    visible from src/internal.h's own declarations."""
    if sys.platform.startswith("linux"):
        return ["-D_DEFAULT_SOURCE", "-D_POSIX_C_SOURCE=200809L"]
    return []


def find_ffi_include_flags():
    """src/internal.h pulls in src/cccc.h, which unconditionally
    `#include <ffi.h>` -- mirror build.c's probe_libffi() fallback so this
    script can compile it standalone without going through --build."""
    pkg_config = shutil.which("pkg-config")
    if pkg_config:
        result = run([pkg_config, "--cflags", "libffi"])
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.split()
    if sys.platform == "darwin":
        for candidate in ("/opt/homebrew/opt/libffi/include",
                           "/usr/local/opt/libffi/include"):
            if Path(candidate).is_dir():
                return ["-I", candidate]
        xcrun = shutil.which("xcrun")
        if xcrun:
            sdk = run([xcrun, "--show-sdk-path"])
            if sdk.returncode == 0 and sdk.stdout.strip():
                return ["-I", f"{sdk.stdout.strip()}/usr/include/ffi"]
        return []
    for candidate in ("/usr/include", "/usr/local/include"):
        if Path(candidate, "ffi.h").is_file():
            return ["-I", candidate]
    return []


def object_has_watched_symbol(obj: Path):
    """Returns the offending symbol name, or None if the object's
    externally-visible ('T'-class) symbols are all clean."""
    nm = shutil.which("nm")
    if not nm:
        # No nm on this host -- can't verify the symbol table directly, but
        # a clean link with no duplicate-symbol errors is still meaningful
        # on its own (the caller already checked that).
        return None
    result = run([nm, "-g", str(obj)])
    if result.returncode != 0:
        return None
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) < 3 or parts[1] not in ("T", "t"):
            continue
        name = parts[2].lstrip("_")
        if any(sub in name for sub in _WATCHED_SYMBOL_SUBSTRINGS):
            return parts[2]
    return None


# build.c's own add_cccc_flags_opt() hardcodes -std=c23, but that's only
# ever exercised under CCCC_BUILD_CC=clang on Linux CI (man/TESTING.md) --
# this script runs standalone against whatever real gcc happens to be on
# PATH, including older ones (e.g. Ubuntu 24.04's gcc-13) that reject
# -std=c23 outright and need the pre-c23 spelling instead. The inline
# linkage semantics this test cares about (__GNUC_STDC_INLINE__) hold under
# either spelling, so falling back doesn't weaken what's being checked.
_STD_FLAG_LADDER = ["-std=c23", "-std=c2x"]


def _compile_with_std_ladder(cc: str, args_tail, opt_flag: str, ffi_flags):
    """Try -std=c23 first, then -std=c2x for older gcc. Returns the
    subprocess result of whichever attempt was last tried."""
    result = None
    for std_flag in _STD_FLAG_LADDER:
        result = run([cc, *args_tail[:2], std_flag, "-Wall", opt_flag, "-g",
                      "-I", str(REPO_ROOT / "src"), *ffi_flags,
                      *platform_defines(), *args_tail[2:]])
        if result.returncode == 0:
            return result
        if "unrecognized command-line option" not in result.stderr and \
           "unrecognized command line option" not in result.stderr:
            break
    return result


def case_two_tu_link(cc: str, opt_flag: str, ffi_flags) -> bool:
    print(f"  two-TU link under {Path(cc).name} {opt_flag} "
          f"(src/internal.h + <ctype.h>/<wctype.h>)")
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        objs = []
        for suffix in ("a", "b"):
            src = tmp_path / f"{suffix}.c"
            src.write_text(_SOURCE_TEMPLATE.format(suffix=suffix))
            obj = tmp_path / f"{suffix}.o"
            compile_result = _compile_with_std_ladder(
                cc, ["-c", str(src), "-o", str(obj)], opt_flag, ffi_flags)
            if compile_result.returncode != 0:
                print(f"    FAIL: compile of {suffix}.c exited "
                      f"{compile_result.returncode}\n"
                      f"    {compile_result.stderr}")
                return False
            offender = object_has_watched_symbol(obj)
            if offender:
                print(f"    FAIL: {suffix}.o externally defines "
                      f"'{offender}' (should stay a discardable "
                      f"extern-inline definition)")
                return False
            objs.append(obj)

        main_src = tmp_path / "main.c"
        main_src.write_text(_MAIN_SOURCE)
        main_obj = tmp_path / "main.o"
        # main.c is plain C89-compatible glue -- no need for the std ladder.
        main_compile = run([cc, "-c", str(main_src), "-o", str(main_obj)])
        if main_compile.returncode != 0:
            print(f"    FAIL: compile of main.c exited "
                  f"{main_compile.returncode}\n    {main_compile.stderr}")
            return False
        objs.append(main_obj)

        exe = tmp_path / "linked"
        link_result = run([cc, *[str(o) for o in objs], "-o", str(exe)])
        if link_result.returncode != 0:
            print(f"    FAIL: link exited {link_result.returncode}\n"
                  f"    {link_result.stderr}")
            return False
    print("    ok")
    return True


def main() -> int:
    print("Host __attribute__-stripping duplicate-symbol link smoke (#1199)")

    cc = find_real_gcc()
    if not cc:
        print("  skipped: no real (non-clang) gcc found on PATH "
              "(checked CCCC_BUILD_CC and " + ", ".join(_GCC_CANDIDATES) + ")")
        return 0

    print(f"  using {cc}")
    ffi_flags = find_ffi_include_flags()
    results = [
        case_two_tu_link(cc, "-O0", ffi_flags),
        case_two_tu_link(cc, "-O2", ffi_flags),
    ]

    if all(results):
        print(f"All {len(results)} host-attribute link smoke cases passed.")
        return 0
    print(f"{results.count(False)} of {len(results)} host-attribute link "
          f"smoke cases FAILED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
