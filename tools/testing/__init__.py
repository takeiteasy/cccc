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
    # --- #1018: RESOLVED. Was: variadic-function serialization gives wrong
    # runtime results. Root cause: <stdarg.h>'s va_start/va_arg/va_copy/
    # va_end macros expanded directly into VM-ABI pointer arithmetic over
    # CCCC's own struct va_list (reg_ptr/stack_ptr/reg_count) and
    # __builtin_frame_address(0) -- the serializer printed that expansion
    # verbatim, which a real host compiler rejects outright ("member
    # reference base type 'va_list' (aka 'char *') is not a structure or
    # union") since the replayed `#include <stdarg.h>` resolves to the real,
    # differently-shaped host va_list at native-compile time. Fixed by
    # wrapping each macro's existing VM-ABI expansion (unchanged) as the
    # trailing argument to a new internal __cccc_va_start/_arg/_copy/_end
    # builtin (src/parse_postfix.c) that parses ap/last/type/src a second,
    # independent time purely to stamp them as serializer annotation
    # (Node.va_form, src/cccc.h) on the returned (otherwise identical) impl
    # node -- VM codegen/comptime/reflection/inlining see byte-identical
    # AST throughout; only serialize_expr (src/serialize.c) reads the
    # annotation, printing the real host `va_start(ap, last)`/`va_arg(ap,
    # type)`/`va_copy(dest, src)`/`va_end(ap)` form instead of walking the
    # VM-internal subtree. No new #include machinery was needed (unlike
    # #1050/#1057): these four only exist as macros, so reaching one at all
    # already requires the user's own `#include <stdarg.h>` in the TU,
    # auto-captured and replayed like any ordinary header.
    #
    # The repro_*.c entries below are dead weight for --native specifically
    # (discover_tests only globs "test_*.c", so repro_*.c is never part of
    # any tools/tests.py corpus, --native included) -- kept here anyway as a
    # record of what #1018's sweep found, since deleting them would lose
    # that trail. Harmless either way: a key discover_tests never produces
    # is never looked up. All but repro_debug.c now round-trip VM->native
    # correctly (confirmed by hand, not through this dict, since none of
    # them are ever looked up); repro_debug.c reads CCCC's own internal
    # va_list.reg_count member directly from user source (not through the
    # va_start/va_arg macros), which has no host equivalent by construction
    # -- expected to keep diverging, not a bug.
    #
    # test_c4_argv.c was retagged here by #967's original corpus sweep
    # despite not using variadic functions at all -- re-triaged and
    # retagged to its own ticket, #1060 (argv[0] naming-convention
    # divergence), not a variadic bug. test_aligned_alloc_vmheap.c was
    # retagged the same way to #1061, but that turned out to be a macOS-only
    # platform gap rather than a general bug -- see NATIVE_SKIP_TESTS_MACOS
    # below, not this table. test_use_system_headers_fallback.c now
    # round-trips correctly and is un-skipped.

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

    # --- found verifying #1090's div/ldiv/lldiv fix: -c=native collapses
    # ldiv_t/lldiv_t (structurally identical on every target this project
    # supports -- long and long long are both 8 bytes) into one printed
    # struct type, so a TU using both gets a bogus type mismatch from the
    # real host compiler. Not a #1090 regression -- the VM itself keeps the
    # two distinct; only -c=native's own struct-type printing conflates
    # them. General serializer defect, not div-family-specific.
    "test_ffi_struct_byval_return_1090.c": "-c=native collapses ldiv_t/lldiv_t into one printed type (#1091)",

    # --- #1020's 7th file (test_constructor_c23.c) and #1083 itself, both
    # RESOLVED: CCCC's own Availability.h stub's `#define __attribute__(x)`
    # used to leak past the first <stdio.h>-pulled-in sys/cdefs.h and strip
    # every later __attribute__ in the TU, including the constructor/
    # destructor ones #1020 taught the serializer to emit. Fixed by guarding
    # both include/Availability.h's own CCCC-flavored body and
    # include/sys/cdefs.h's Availability.h include on #ifdef __CCCC__,
    # handing off to the real host Availability.h (via __has_include_next)
    # otherwise -- so test_constructor_c23.c now round-trips VM 42 ->
    # native 42 and no longer needs an entry here ---

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
    # --- #1022, closed this pass: pthread/threads native support gaps ---
    # Root cause was `-I./include`'s host cc reading CCCC's own polyfill
    # pthread_mutex_t/pthread_cond_t projection (24/16 bytes) instead of the
    # real host layout (64/48 bytes on macOS arm64) while linking against
    # real libpthread -- silent memory corruption, not a compile/link
    # failure, so test_pthread_mutex.c (never skipped) was flaking non-42
    # exit codes. Fixed by giving include/pthread.h a real #include_next
    # hand-off (#1021/#1040-style), a narrow PTHREAD_MUTEX/COND_INITIALIZER
    # re-emission fix (src/serialize.c), and emitting `_Thread_local` for
    # Obj.is_tls (previously silently dropped). See #1022's own close
    # comment / NATIVE_1018_PLAN.md for the full writeup.
    #
    # test_pthread_recursive_mutex.c and test_pthread_nonrecursive_
    # deadlock_detect.c dropped from this table: the former now round-trips
    # VM 42 -> native 42; the latter is a --thread-safety VM diagnostic
    # (EDEADLK from re-locking a default mutex) with no host equivalent --
    # it now auto-skips via NATIVE_VM_ONLY_FLAGS instead (it used to hang
    # forever natively, since a real default mutex genuinely deadlocks).
    #
    # The four <threads.h> files (thrd_create/mtx_lock/tss_create/etc.) are
    # a distinct, larger problem -- those are VM cfuncs (src/stdlib/
    # pthread.c) with no host libc symbol to link against at all on any
    # platform, and no real <threads.h> exists on macOS in the first place.
    # Retagged to #1088, not fixed here.
    "test_thread_local_isolation.c": "no <threads.h> lowering for -c=native (#1088)",
    "test_threads_basic.c": "no <threads.h> lowering for -c=native (#1088)",
    "test_threads_mutex.c": "no <threads.h> lowering for -c=native (#1088)",
    "test_threads_tss.c": "no <threads.h> lowering for -c=native (#1088)",
    # test_pthread_cond.c dropped from this table too: the aarch64-Linux
    # pthread_cond_wait() "deadlock" recorded here previously (found while
    # verifying #1067, unrelated to it) turned out to be the *same* root
    # cause as the rest of #1022, not a distinct glibc condvar bug --
    # -I./include shadowed the real pthread_cond_t layout with CCCC's own
    # 16-byte polyfill projection, corrupting the condvar's internal state.
    # Re-verified directly in the cccc-linux-arm64 container (image rebuilt
    # from current source): compiles and round-trips VM 42 -> native 42,
    # 8/8 repeated runs clean, no flakiness. No separate ticket needed.

    # --- #1034 survivors: 1 distinct cause remaining, retagged (10 of the
    # original 13 files fixed and dropped from this table entirely -- 5 in
    # #1034's own pass, test_comptime_decl_index_anon_array_951.c fixed
    # separately under #1046, test_comptime_and_runtime_fn_ptr_tables_309.c
    # fixed separately under #1045, test_comptime_ptr_303.c fixed
    # separately under #1049, test_comptime_decl_index_unused_894.c fixed
    # separately under #1047, and test_comptime_include_boundary_890.c fixed
    # separately under #1048 -- see #1034's own close comment for what
    # landed and where) ---
    "test_macros_quote_args_splice.c": "CCCC's internal va_list layout leaks into native output via __builtin_quote arg-splicing -- same class as #1018, different repro path (#1018)",

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
    "test_c4_argv.c": "asserts argv[0]/argv[1] against the VM's own "
                 "bytecode-mode naming convention (argv[0] ending in "
                 "'.c4', mimicking the bytecode file `cccc file.c` would "
                 "have produced); a natively-compiled binary's argv[0] is "
                 "whatever the OS execve() gives it, with no bytecode file "
                 "involved and no host equivalent to translate the "
                 "convention to, see COVERAGE.md Serialized-output "
                 "divergences (#1060)",

    # --- #1076: RESOLVED on the VM side (a block literal inside a
    # genuinely nested function, capturing a variable owned by one of that
    # function's own ancestors, was silently miscompiled -- wrong answer,
    # no diagnostic, no -c=native involvement at all). Fixed by recording
    # the same block_outer_locals ancestor-climb snapshot for nested
    # functions that block literals already record for each other
    # (parse_decl.c/parse_blocks.c), and by adding the missing
    # belongs_to_outer_function()-based static-link-chase arm to the
    # block's own capture-population loop (codegen_expr.c), sharing
    # gen_addr's own chase via the new emit_static_chain_var_addr
    # (codegen_addr.c) so the two can't drift apart the way #994 warned
    # about. -c=native itself stays out of scope (#1074's own rejection for
    # this exact shape, "a block literal inside a nested function capturing
    # ... is not supported", now fires as intended instead of being dead
    # code -- see #1080, the native-lowering follow-up).
    "test_block_in_nested_1076.c": "a block literal inside a nested "
                 "function capturing a variable owned by one of that "
                 "function's own ancestors has no native lowering yet "
                 "(#1074's own rejection); the VM-side miscompile itself "
                 "is fixed (#1076), see #1080",

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
    # test_math_c23_ieee.c: three distinct blockers, all confirmed through
    # the real -I./include native.py-shaped compile in the cccc-linux-amd64
    # container -- not a single macOS-only gap as first thought. (1) macOS's
    # libm genuinely lacks the whole C23 fmaximum/fminimum/totalorder/etc
    # family (#1037, RESOLVED WONT_FIX -- permanent platform gap, same
    # reasoning as #1028/reallocarray). (2) -c=native's native `cc`
    # invocation never passed -lm at all -- glibc 2.34+ only folded the
    # *common* math symbols (sin/sqrt/etc) into libc.so.6, not this newer
    # C23 family (still libm-only there) -- RESOLVED (#1051, src/main.c now
    # always appends -lm). (3) With (2) fixed, a third and unrelated blocker
    # surfaced on Linux: __cccc_issignaling_d/__cccc_iseqsig_d (backing the
    # issignaling()/iseqsig() macros) were CCCC-internal names with no real
    # libc equivalent to link against and no serializer-emitted definition
    # either -- RESOLVED (#1052, native_accessor_shims,
    # src/serialize.c:3931, extended to these four names). A fourth blocker
    # (#1066) was found on Linux after this comment first claimed a clean
    # round-trip there: fromfp(1e10, FP_INT_TONEAREST, 8)'s overflow case
    # returned a different *value* under -c=native than the VM (127 vs 0).
    # Turned out not to be a bug at all -- C23 7.12.9.6 only guarantees
    # FE_INVALID on overflow, the returned value is implementation-defined,
    # and real glibc's choice (saturate) differs from CCCC's VM (return 0)
    # while both conform. RESOLVED (#1066, the test's over-specified value
    # checks relaxed to check FE_INVALID only; see include/math.h's
    # fromfp/ufromfp comment and COVERAGE.md for the corrected contract). A
    # fifth blocker (#1079) surfaced once (1)-(4) were fixed:
    # cccc_setpayload_impl/cccc_setpayloadf_impl (src/stdlib/math.c) left
    # the destination untouched on a failed setpayload*/setpayloadsig*
    # call instead of zeroing it, unlike real glibc -- RESOLVED (#1079,
    # both impls now zero the destination on every failure path). Only (1)
    # remains, and it's macOS-only -- kept here (not the general
    # NATIVE_SKIP_TESTS table) now that Linux round-trips cleanly.
    "test_math_c23_ieee.c": "macOS libm lacks the C23 fmaximum/fminimum/"
                             "totalorder/etc family (#1037, WONT_FIX, "
                             "permanent platform gap)",
    # test_setpayload_zero_1079.c: same #1037 platform gap as
    # test_math_c23_ieee.c above -- Darwin's libm declares no setpayload/
    # setpayloadf/setpayloadl/setpayloadsig/setpayloadsigf/setpayloadsigl
    # at all, so the replayed real host <math.h> leaves every one of them
    # an undeclared identifier under -c=native on macOS. Round-trips
    # cleanly on Linux (glibc >= 2.35).
    "test_setpayload_zero_1079.c": "macOS libm lacks setpayload/"
                             "setpayloadsig and family entirely (#1037, "
                             "WONT_FIX, permanent platform gap)",
    # test_aligned_alloc_vmheap.c: calls aligned_alloc(alignment, 128) for
    # alignment in {8,16,32,64,256} -- 128 is not a multiple of 256. C11
    # originally required size to be an integral multiple of alignment, but
    # DR 460/N2072 removed that constraint in C17; glibc 2.38+ implements
    # the C17 wording and accepts it (confirmed directly this pass, both
    # arches, in the cccc-linux-amd64/cccc-linux-arm64 containers: errno==0,
    # a valid pointer back). macOS libc still implements the pre-DR-460 C11
    # rule and returns NULL/EINVAL for the same call. CCCC's VM-heap
    # aligned_alloc (MALCA, src/ops.c) implements the C17/C23 wording, same
    # as glibc -- so this is macOS libc lagging the standard CCCC targets
    # (defaults to C23), not VM leniency to tighten (#1061, RESOLVED
    # WONT_FIX -- an earlier triage pass's premise that "glibc and macOS
    # both reject it" didn't hold up under direct measurement). Round-trips
    # VM 42 -> native 42 on Linux, confirmed through the real
    # -I./include-forwarding container shape -- un-skipped there, only
    # macOS-skipped here.
    "test_aligned_alloc_vmheap.c": "macOS libc still enforces the pre-C17 "
                                    "aligned_alloc size-must-be-a-multiple "
                                    "rule that DR 460 removed; glibc 2.38+ "
                                    "and the VM heap both implement C17/C23 "
                                    "(#1061, WONT_FIX, permanent platform "
                                    "gap)",
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
    # #1022: `--thread-safety`'s double-lock (EDEADLK) diagnostic is a CCCC
    # VM enforcement layer -- a real default (non-recursive) pthread mutex
    # re-locked by the same thread genuinely deadlocks on the real host
    # instead, matching the existing "VM-only enforcement dropped by
    # -c=native" warning `cccc -c=native --thread-safety` already prints.
    # Without this, test_pthread_nonrecursive_deadlock_detect.c's native
    # binary hangs forever (confirmed) instead of skipping cleanly.
    "--thread-safety",
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
