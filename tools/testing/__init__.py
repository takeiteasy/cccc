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


# Tests that cannot survive the -c=native round-trip (tools/testing/native.py,
# ticket #967). Unlike C4_SKIP_TESTS this is a dict, not a set: {filename:
# "reason (#ticket)"}, grouped by bug class under a comment header per group,
# so the reason surfaces through the existing skip_reason plumbing
# (report.py) and each entry can be deleted individually as its ticket
# closes. Populated from a full-corpus sweep done while building #967 --
# every entry here compiled and ran correctly through the VM but diverged
# under -c=native (wrong compile, wrong link, or wrong runtime result).
NATIVE_SKIP_TESTS = {
    # --- #1018: variadic-function serialization gives wrong runtime results ---
    # The repro_*.c entries below are dead weight for --native specifically
    # (discover_tests only globs "test_*.c", so repro_*.c is never part of
    # any tools/tests.py corpus, --native included) -- kept here anyway as a
    # record of what #1018's sweep found, since deleting them would lose
    # that trail. Harmless either way: a key discover_tests never produces
    # is never looked up.
    "repro_1to5.c": "variadic serialization gives wrong result (#1018)",
    "repro_basic_var.c": "variadic serialization gives wrong result (#1018)",
    "repro_debug.c": "variadic serialization gives wrong result (#1018)",
    "repro_exact.c": "variadic serialization gives wrong result (#1018)",
    "repro_inner_vararg.c": "variadic serialization gives wrong result (#1018)",
    "repro_longname.c": "variadic serialization gives wrong result (#1018)",
    "repro_minvar.c": "variadic serialization gives wrong result (#1018)",
    "repro_mixed_args.c": "variadic serialization gives wrong result (#1018)",
    "repro_namesize.c": "variadic serialization gives wrong result (#1018)",
    "repro_nested2.c": "variadic serialization gives wrong result (#1018)",
    "repro_nested3.c": "variadic serialization gives wrong result (#1018)",
    "repro_nested4.c": "variadic serialization gives wrong result (#1018)",
    "repro_nested5.c": "variadic serialization gives wrong result (#1018)",
    "repro_nested_vararg.c": "variadic serialization gives wrong result (#1018)",
    "repro_nested_varargs.c": "variadic serialization gives wrong result (#1018)",
    "repro_only_nested.c": "variadic serialization gives wrong result (#1018)",
    "repro_onlytest5.c": "variadic serialization gives wrong result (#1018)",
    "repro_short.c": "variadic serialization gives wrong result (#1018)",
    "repro_simple_loop.c": "variadic serialization gives wrong result (#1018)",
    "repro_va_copy.c": "variadic serialization gives wrong result (#1018)",
    "repro_vasize.c": "variadic serialization gives wrong result (#1018)",
    "repro_withr.c": "variadic serialization gives wrong result (#1018)",
    "repro_znames.c": "variadic serialization gives wrong result (#1018)",
    "test_float_arg_marshalling.c": "variadic serialization gives wrong result (#1018)",
    "test_math_ffi_signatures.c": "variadic serialization gives wrong result (#1018)",
    "test_c4_argv.c": "variadic serialization gives wrong result (#1018)",
    "test_use_system_headers_fallback.c": "variadic serialization gives wrong result (#1018)",
    "test_aligned_alloc_vmheap.c": "variadic serialization gives wrong result (#1018)",

    # --- #1019: RESOLVED. Was: vector_size arithmetic/select fails or
    # misbehaves under -c=native. Two root causes, both in serialize.c:
    # (1) a scalar operand of `vector op scalar` was re-emitted with an
    # explicit `(v4si)scalar` cast -- the type checker's internal
    # broadcast marker, not source-level C; GCC/clang perform that
    # broadcast implicitly inside the operator and reject it spelled as a
    # cast. (2) GNU per-lane `?:` (a vector-typed condition) re-emitted
    # verbatim as `cond ? a : b` -- a GCC-only extension clang rejects;
    # now lowered to portable mask arithmetic. test_attr_vector_size_
    # variadic.c compiles now (unaffected by either fix) but still gives a
    # wrong runtime result -- that is #1018's variadic-marshalling class,
    # not a vector one, so its skip entry moved there instead of closing.

    # --- #1018: variadic-function serialization gives wrong runtime
    # results (see the block at the top of this dict for the rest) ---
    "test_attr_vector_size_variadic.c": "variadic serialization gives wrong result (#1018)",

    # --- #1020: constructor/destructor ordering wrong under native ---
    "test_constructor_basic.c": "constructor/destructor ordering wrong under native (#1020)",
    "test_constructor_c23.c": "constructor/destructor ordering wrong under native (#1020)",
    "test_constructor_priority.c": "constructor/destructor ordering wrong under native (#1020)",
    "test_constructor_destructor_static_opt.c": "constructor/destructor ordering wrong under native (#1020)",
    "test_destructor_exit_atexit_order.c": "constructor/destructor ordering wrong under native (#1020)",
    "test_destructor_exit_reentrant.c": "constructor/destructor ordering wrong under native (#1020)",
    "test_destructor_on_exit.c": "constructor/destructor ordering wrong under native (#1020)",

    # --- split out of #1021 (fenv.h/math.h identifiers fail to compile),
    # now RESOLVED -- three distinct files that compile cleanly now but each
    # fail for a different, unrelated reason discovered while fixing it ---
    # test_fenv.c (#1035, RESOLVED: serialize.c's ND_CAST case now
    # recognizes the FE_DFL_ENV sentinel cast and emits the bare
    # identifier, which resolves via <fenv.h>'s #include_next to the host's
    # real header) and test_float_to_int_conversion.c (#1036, RESOLVED as a
    # duplicate of #1038 -- the "divergence" was #1038's unsuffixed/
    # low-precision float-literal printer producing the wrong expected
    # value in the *test*, not a real saturating-cast semantics gap; once
    # #1038 fixed the literal, VM and native agree) no longer need an entry
    # here.
    "test_math_c23_ieee.c": "macOS libm lacks the C23 fmaximum/fminimum/"
                   "totalorder/etc family -- link failure, not a compile "
                   "error (#1037)",

    # --- #1022: pthread/threads native support gaps ---
    "test_thread_local_isolation.c": "threads fail under native (#1022)",
    "test_threads_basic.c": "threads fail under native (#1022)",
    "test_threads_mutex.c": "threads fail under native (#1022)",
    "test_threads_tss.c": "threads fail under native (#1022)",
    "test_pthread_nonrecursive_deadlock_detect.c": "threads fail under native (#1022)",
    "test_pthread_recursive_mutex.c": "threads fail under native (#1022)",

    # --- singleton bugs, one ticket each ---
    "test_minilua.c": "4 unrelated native-compile bugs found post-#1027 (#1042)",
    "test_use_system_headers_setjmp.c": "jmp_buf mismatch under --use-system-headers (#1030)",
    # The emitted C replays `#include <sys/mount.h>` verbatim, so the host
    # header supplies the real ~2100-byte struct statfs and member access
    # re-resolves correctly against it -- but `sizeof(struct statfs)` was
    # already constant-folded guest-side against CCCC's ~56-byte
    # projection and is baked into the emitted TU as a plain integer
    # literal, so the malloc'd buffer is undersized and the real host
    # statfs() overruns it (the canary is clobbered). General soundness
    # class -- any folded sizeof/offsetof over a CCCC-projected system
    # struct, not statfs-specific -- sibling to the FP_* constant-folding
    # note at src/serialize.c's native_accessor_shims comment. Still open.
    "test_sys_mount_statfs.c": "guest-side folded sizeof(struct statfs) disagrees with the host header's real layout, host statfs() overruns the buffer (#1031)",

    # --- #1034: comptime/macro-generated declarations fail to serialize ---
    "test_ast_builders_296.c": "comptime-generated decl fails to serialize (#1034)",
    "test_comptime_and_runtime_fn_ptr_tables_309.c": "comptime-generated decl fails to serialize (#1034)",
    "test_comptime_decl_index_anon_array_951.c": "comptime-generated decl fails to serialize (#1034)",
    "test_comptime_decl_index_unused_894.c": "comptime-generated decl fails to serialize (#1034)",
    "test_comptime_include_boundary_890.c": "comptime-generated decl fails to serialize (#1034)",
    "test_comptime_ptr_303.c": "comptime-generated decl fails to serialize (#1034)",
    "test_custom_attributes_serialize_235.c": "comptime-generated decl fails to serialize (#1034)",
    "test_global_block_expansion.c": "comptime-generated decl fails to serialize (#1034)",
    "test_macros_generate_constructor_235.c": "comptime-generated decl fails to serialize (#1034)",
    "test_macros_local_var.c": "comptime-generated decl fails to serialize (#1034)",
    "test_macros_quote_multi_stmt_global.c": "comptime-generated decl fails to serialize (#1034)",
    "test_macros_stdlib_memcpy_strlen_strcmp_235.c": "comptime-generated decl fails to serialize (#1034)",
    "test_macros_quote_args_splice.c": "comptime-generated decl fails to serialize (#1034)",

    # --- by-design divergence, not a bug: see COVERAGE.md ---
    "test_c4.c": "old-style implicit-int main() -- VM leniency the host "
                 "compiler doesn't share, see COVERAGE.md Serialized-output "
                 "divergences",
    "test_edge_void_main_stray_block.c": "asserts a `void main()` that "
                 "falls off its end -- codegen unconditionally loads 0 "
                 "into the return register at the end of the entry "
                 "function regardless of return type, so the VM always "
                 "exits 0; the host compiler leaves a void main's exit "
                 "status undefined (the test's own header already calls "
                 "this UB), so native inherits whatever the ABI left in "
                 "the return register, see COVERAGE.md Serialized-output "
                 "divergences (#1031)",
    "test_sys_ioctl_standalone.c": "asserts CCCC's VM-side wrap_ioctl() "
                 "request-code allowlist (#795) rejects an unverified raw "
                 "ioctl request; -c=native calls the real host ioctl() "
                 "directly, which has no such allowlist to reject with, "
                 "see COVERAGE.md Serialized-output divergences",
    "test_alloca_no_block_reclaim.c": "asserts __builtin_alloca() addresses "
                 "stay distinct across loop iterations sharing a block with "
                 "a genuine VLA; the real host compiler (confirmed: clang "
                 "-O0) legitimately reuses the same alloca slot each "
                 "iteration via a stacksave/stacksave-restore pair scoped "
                 "to the VLA's block, unlike the VM's separate per-AllocKind "
                 "lifetimes (#981) -- the alloca call itself now compiles "
                 "correctly (#1024, emitted as __builtin_alloca), see "
                 "COVERAGE.md Serialized-output divergences",
    "test_main_bad_argc_error.c": "source is deliberately a bad main() "
                 "signature to test -Wmain; the host compiler treats it as "
                 "a hard error rather than a warning, see COVERAGE.md",
    "test_warning_main_bad_params.c": "source is deliberately a bad main() "
                 "signature to test -Wmain; the host compiler treats it as "
                 "a hard error rather than a warning, see COVERAGE.md",
    "test_warning_declarations_default.c": "source deliberately falls off "
                 "the end of a non-void function to test the default "
                 "return-type warning; the host compiler treats it as a "
                 "hard error rather than a warning, see COVERAGE.md",
    "test_warning_return_type.c": "source deliberately falls off the end "
                 "of a non-void function to test -Wreturn-type; the host "
                 "compiler treats it as a hard error rather than a "
                 "warning, see COVERAGE.md",
    "test_warning_implicit_function_ffi.c": "source deliberately calls an "
                 "undeclared function to test "
                 "-Wimplicit-function-declaration; the host compiler "
                 "treats it as a hard error under C23, see COVERAGE.md",
    "test_use_system_headers_pragma_suppress.c": "source deliberately "
                 "leaves an unterminated '#pragma clang assume_nonnull' "
                 "open across the file to test pragma-noise suppression; "
                 "the host compiler's own pragma balance check rejects it",

    # --- no compiled artifact to run (frontend-only invocation) ---
    "test_version.c": "--version prints and exits; no program to compile",
}

# Platform-specific -c=native skips, checked only when the running host
# matches -- unlike NATIVE_SKIP_TESTS these are not bugs to fix, so they
# aren't tied to a follow-up ticket that ever closes. reallocarray() is a
# permanent macOS platform gap (#1028, decided: documented in COVERAGE.md,
# no polyfill), not a bug -- it still runs (and should keep running) through
# --native on Linux/glibc, the platform --native is meant to land in once
# wired into CI.
NATIVE_SKIP_TESTS_MACOS = {
    "test_reallocarray.c": "reallocarray undefined on macOS libc, permanent "
                            "platform gap (#1028), still exercised on Linux",
}

# CLI flags that -c=native drops with a warning rather than enforcing
# (#935's VM-only-enforcement decision) -- exercising them natively would
# silently test nothing, so they're skipped rather than run with the safety
# net quietly missing.
NATIVE_VM_ONLY_FLAGS = {
    "--checked-pointers",
    "--bounds-checks", "-B",
    "--uaf-detection",
    "--type-checks",
    "--heap-canaries",
    "--memory-leak-detection", "-M",
    "--memory-tagging",
    "--pointer-sanitizer", "-P",
    "--uninitialized-detection",
    "--posix-emulation",
    "--stack-canaries",
    "-V",
    "-1", "-2", "-3",
    "--safety=basic", "--safety=standard", "--safety=max",
    # CCCC FFI policy options: "-c=native cannot be combined with CCCC FFI
    # policy options" (main.c) -- these govern the VM's own FFI call path
    # and have no native-mode equivalent (a native binary calls libc
    # directly, with no FFI layer to police).
    "--ffi-allow", "--ffi-deny", "--disable-ffi", "-F",
    "--ffi-errors-fatal", "--ffi-type-checking",
}

# Frontend/output modes -c=native can't be combined with, or that make the
# per-test flags drive their own -c/-o (so the runner's own -c=native/-o
# would collide). Checked by prefix against each per-test flag.
_NATIVE_FRONTEND_PREFIXES = (
    "-E", "-m", "--dump-", "--diagnostics-json", "--json", "-j", "-d", "--disassemble",
    "-c=generated", "-c=gen", "-c=g", "-cgenerated", "-cgen", "-cg",
    "--compile=generated", "--compile=gen", "--compile=g",
    "--version", "-h", "--help",
)


def native_skip_reason(filename, per_test_flags, cccc_args, platform=None):
    """Return a skip_reason string if this test cannot go through the
    -c=native round-trip, else None. Called by native.py before the compile
    step; the per-test CCCC_FLAGS scan mirrors runner.py's own header parse.
    """
    if filename in NATIVE_SKIP_TESTS:
        return NATIVE_SKIP_TESTS[filename]
    if platform == "macos" and filename in NATIVE_SKIP_TESTS_MACOS:
        return NATIVE_SKIP_TESTS_MACOS[filename]
    all_flags = list(cccc_args) + list(per_test_flags)
    for f in all_flags:
        if f == "--build" or f.startswith("--build="):
            return "--build mode"
        if f == "--testing" or f.startswith("--test"):
            return "--testing: no --test-native (#1033)"
        if f in ("-c", "-o", "--out") or f.startswith("-c=") or f.startswith("-o=") \
                or f.startswith("--compile") or f.startswith("--out="):
            return "test drives -c/-o itself"
        for p in _NATIVE_FRONTEND_PREFIXES:
            if f == p or f.startswith(p):
                return "frontend output mode"
        if f in NATIVE_VM_ONLY_FLAGS or any(
            f.startswith(v + "=") for v in NATIVE_VM_ONLY_FLAGS if v.startswith("--")
        ):
            return f"VM-only enforcement dropped by -c=native ({f})"
    return None


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
