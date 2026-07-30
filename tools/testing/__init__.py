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

# CLI flags that make cccc key safety checks off AllocHeader metadata, which
# only exists for VM-heap allocations: CCCC_VM_HEAP_TRIGGERS in src/cccc.h
# (minus CCCC_VM_HEAP itself) plus the -1/-2/-3/--safety=* presets that
# already imply it. The leak pass normally injects -V so guest malloc/free
# become individually visible to `leaks`/valgrind, but -V disables the VM
# heap -- combined with any of these flags, cccc now refuses to run at all
# (#845), so the leak pass must drop -V for tests that use them instead of
# getting a bogus "0 leaks" or a segfault. Guest allocations become invisible
# to the leak tool in that case, but cccc's own host-side allocations are
# still checked.
LEAKS_VM_HEAP_DEPENDENT_FLAGS = {
    "--bounds-checks", "-B",
    "--uaf-detection",
    "--type-checks",
    "--heap-canaries",
    "--memory-leak-detection", "-M",
    "--memory-tagging",
    "--pointer-sanitizer", "-P",
    "-1", "-2", "-3",
    "--safety=basic", "--safety=standard", "--safety=max",
}


def leak_pass_wants_vm_heap(cccc_args, per_test_flags, keep_vm_heap_annotated=False):
    """True if any effective flag for this test requires the VM heap, so the
    leak pass must not pass -V (see LEAKS_VM_HEAP_DEPENDENT_FLAGS).

    keep_vm_heap_annotated is set from a CCCC_LEAKS_KEEP_VM_HEAP header
    annotation (tools/testing/runner.py) for tests whose VM-heap dependence
    can't be seen from CCCC_FLAGS at all -- e.g. a runtime builtin
    (__builtin_dynamic_object_size) whose resolution silently degrades to
    the conservative fallback for non-VM-heap pointers, or a per-subtest
    [[cccc::test(flags = "...")]] attribute the static header scan can't
    parse. Withholding -V there avoids both a false "test failed" (assertion
    against the degraded fallback) and a false MEMORY LEAK (#845).
    """
    if keep_vm_heap_annotated:
        return True
    all_flags = set(cccc_args) | set(per_test_flags)
    return not all_flags.isdisjoint(LEAKS_VM_HEAP_DEPENDENT_FLAGS)
