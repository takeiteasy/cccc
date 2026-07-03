"""Platform detection for the CCCC test runner."""

import os


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
