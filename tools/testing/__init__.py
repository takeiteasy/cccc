"""CCCC test infrastructure package.

REPO_ROOT is the single source of truth for the repository root path.
All sub-modules import it from here; no module recomputes .parent.parent
on its own __file__ (which would produce wrong results at different depths).
"""

import os
from pathlib import Path

# tools/testing/ → tools/ → repo root
REPO_ROOT = Path(__file__).resolve().parent.parent.parent

def vm_profile_path(profile_dir, test_name, mode):
    """Return a Path for the VM opcode profile JSON, or None if profiling is off."""
    if not profile_dir:
        return None
    safe = test_name.replace(os.sep, "__").replace("/", "__")
    return Path(profile_dir) / f"{safe}.{mode}.json"


# Tests that cannot survive the .c4 bytecode round-trip
# (FFI-table rehydration or stack-size constraints).
C4_SKIP_TESTS = {
    "test_ffi_fatal_error.c",
    "test_ffi_type_check_arity.c",
    "test_stack_overflow_large_frame.c",
}

# Tests that hang under macOS `leaks -atExit` due to fork()/wait() interactions
# with the leak instrumentation.  The child inherits MallocStackLogging hooks;
# if the child calls exit() or triggers a signal the library's atexit handler
# fires in the child and sends SIGSTOP, causing the parent's waitpid to block.
# Tracked: #574.
LEAKS_SKIP_TESTS = {
    "test_posix_sys_wait.c",
    "test_exit_code.c",
    "test_build_parallel_targets.c",
    "test_build_parallel_keep_going.c",
}
