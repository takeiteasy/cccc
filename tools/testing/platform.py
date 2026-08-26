"""Platform detection for the CCCC test runner."""

import os
import subprocess


def detect_platform():
    system = os.uname().sysname if hasattr(os, "uname") else os.name
    if system == "Darwin":
        return "macos"
    elif system == "Linux":
        return "linux"
    elif system in ("CYGWIN", "MINGW", "MSYS", "Windows"):
        return "windows"
    else:
        return "unknown"


_native_cc_family_cache = {}


def detect_native_cc_family(cc=None):
    """Return "clang", "gcc", or "unknown" for the compiler -c=native's own
    host-backend actually shells out to (cccc_find_native_cc(), src/vm.c) --
    honors the same CCCC_NATIVE_CC override, falling back to "cc" (the
    unqualified PATH lookup the C side tries first).

    #1186: the axis that decides whether a NATIVE_SKIP_TESTS entry is stale
    turned out to be compiler *family*, not GCC version or host platform --
    see man/TESTING.md's "Native round-trip mode" section. `cc -dM -E -`
    (an empty translation unit, just dumping predefined macros) is the
    standard portable way to ask a compiler what it is: clang defines
    __clang__, plain gcc does not. Cached per `cc` value -- this runs once
    per sub-suite invocation, not once per test, but even that would be
    cheap (a single fast, no-input compiler invocation).
    """
    cc = cc or os.environ.get("CCCC_NATIVE_CC") or "cc"
    if cc in _native_cc_family_cache:
        return _native_cc_family_cache[cc]

    family = "unknown"
    try:
        result = subprocess.run(
            [cc, "-dM", "-E", "-"], input="", capture_output=True,
            text=True, timeout=10,
        )
        if result.returncode == 0:
            if "__clang__" in result.stdout:
                family = "clang"
            elif "__GNUC__" in result.stdout:
                family = "gcc"
    except (OSError, subprocess.TimeoutExpired):
        pass

    _native_cc_family_cache[cc] = family
    return family
