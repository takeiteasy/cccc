// build.c — self-hosting build script for CCCC.
//
// Bootstrap: the first build still requires the Makefile (make cccc), which
// produces a *bootstrap* compiler good enough to run this script. Subsequent
// rebuilds: ./cccc --build build.c
//
// Default build (./cccc --build build.c) is a two-pass build (#571/#842):
//   1. "cccc-pass1" is built against the current src/std.c.
//   2. cccc-pass1 regenerates src/std.c from tools/generate_stdlib.c
//      (tools/regen_stdlib.sh — atomic, a no-op if nothing changed).
//   3. "cccc" is (re)compiled against the regenerated src/std.c.
// src/std.c can therefore never go stale; `make stdlib` is never needed.
//
// Selective builds via --build-target=NAME:
//   cccc_asan          AddressSanitizer + UBSan build  → cccc-asan
//   cccc_ubsan         UndefinedBehaviourSanitizer      → cccc-ubsan
//   cccc_tsan          ThreadSanitizer                  → cccc-tsan
//   cccc_msan          MemorySanitizer (Linux+clang)    → cccc-msan
//   sanitizers         all of the above in one graph
//   fuzz_harness       libFuzzer + ASan harness         → fuzz_harness
//   afl / afl_asan     AFL++ builds (needs afl-clang-fast/afl-clang)
//   stdlib_gen         two-pass regenerate src/std.c only (no final rebuild)
//   bench              hyperfine benchmark
//   bench_compare{,_quick,_json}, profile_cpu, profile_mem, dsym
//   clean, host_tests, test / test_suites / test_legacy, sqlite_smoke,
//   header_resolution_smoke, cli_exit_code_smoke, comptime_native_smoke,
//   audit_ffi,
//   audit_reflection_enums, reflection_ffi_gen / _check
//   docs                Doxygen HTML API docs for include/cccc/*.h (needs
//   doxygen)
//   build_cache_toolchain_smoke   #1198 regression guard (same-arch,
//   different-compiler-family --build-cache reuse; skips if only one
//   compiler family is on PATH) linux_amd64_build /
//   linux_aarch64_build    build the Colima container image linux_amd64_smoke /
//   linux_aarch64_smoke    + container arch/exit-42 sanity check
//   linux_amd64_test / linux_aarch64_test      + full test suite (amd64 is
//   5-way sharded) linux_amd64_msan_test                      + cccc-msan build
//   + full suite (#844 known-noise)
//
// Not covered here — use tools/Makefile.backup for:
//   Cross-compiling for a 4th Colima profile / other architectures not listed
//   above.

#include <stdio.h>
#include <string.h>

// Forward declaration: defined near stdlib_gen() below, used earlier by
// release() (#883) to regenerate src/std.c before the release build.
static BuildTarget *stdlib_regen_step(Builder *ctx, BuildTarget *bt);

// ---- libffi probing -------------------------------------------------------

static void probe_libffi(Builder *ctx, BuildTarget *t) {
    if (HaveTool(ctx, "pkg-config") && PkgConfig(t, "libffi") == 0)
        return;
    // pkg-config not available or failed; fall back to well-known paths
    if (strcmp(BuildHost(ctx), "darwin") == 0) {
        AddInclude(t, "/opt/homebrew/opt/libffi/include");
        AddInclude(t, "/usr/local/opt/libffi/include");
        AddLibPath(t, "/opt/homebrew/opt/libffi/lib");
        AddLibPath(t, "/usr/local/opt/libffi/lib");
    } else {
        AddInclude(t, "/usr/include");
        AddInclude(t, "/usr/local/include");
    }
    AddLib(t, "ffi");
}

// ---- libcurl (optional; controlled by CCCC_HAS_CURL env var) -------------

static void maybe_add_curl(Builder *ctx, BuildTarget *t) {
    const char *v = GetEnv(ctx, "CCCC_HAS_CURL");
    if (!v || strcmp(v, "0") == 0)
        return;
    AddDefine(t, "CCCC_HAS_CURL", "1");
    if (HaveTool(ctx, "pkg-config") && PkgConfig(t, "libcurl") == 0)
        return;
    if (strcmp(BuildHost(ctx), "darwin") == 0) {
        AddInclude(t, "/opt/homebrew/opt/curl/include");
        AddInclude(t, "/usr/local/opt/curl/include");
        AddInclude(t, "/opt/homebrew/include");
        AddLibPath(t, "/opt/homebrew/opt/curl/lib");
        AddLibPath(t, "/usr/local/opt/curl/lib");
        AddLibPath(t, "/opt/homebrew/lib");
    } else {
        AddInclude(t, "/usr/include");
        AddInclude(t, "/usr/local/include");
    }
    AddLib(t, "curl");
}

// ---- Intel BID decimal FP library (optional; controlled by CCCC_HAS_DECIMAL)
// -- Never vendored (see tools/fetch_intel_bid.sh); fetched/built on demand
// into a gitignored prefix. Mirrors Makefile.backup's CCCC_HAS_DECIMAL block,
// one difference: a missing libbid.a here degrades to a non-decimal build with
// a diagnostic rather than aborting the whole graph (add_cccc_flags(), which
// calls this, has no way to fail its caller's target outright).

static void maybe_add_decimal(Builder *ctx, BuildTarget *t) {
    const char *v = GetEnv(ctx, "CCCC_HAS_DECIMAL");
    if (!v || strcmp(v, "0") == 0)
        return;
    const char *prefix = GetEnv(ctx, "CCCC_BID_PREFIX");
    if (!prefix || !*prefix)
        prefix = "build/intel-bid";
    char bid_a[512], bid_inc[512];
    snprintf(bid_a, sizeof(bid_a), "%s/lib/libbid.a", prefix);
    snprintf(bid_inc, sizeof(bid_inc), "%s/src", prefix);
    if (!FileExists(ctx, bid_a)) {
        fprintf(stderr,
                "build: CCCC_HAS_DECIMAL=1 but %s is missing — run "
                "tools/fetch_intel_bid.sh "
                "first. Building without decimal FP support.\n",
                bid_a);
        return;
    }
    AddDefine(t, "CCCC_HAS_DECIMAL", "1");
    AddInclude(t, bid_inc);
    AddLdFlag(t, bid_a);
}

// ---- ndbm on Linux (optional; controlled by CCCC_HAS_NDBM env var) --------
// <ndbm.h> is macOS/BSD-native; on Linux it's reachable only via
// libgdbm-compat (real header, byte-identical to gdbm-ndbm.h, plus
// libgdbm_compat.{so,a}) -- an extra dependency for a legacy interface, so
// this stays opt-in rather than probed-for-and-silently-enabled like
// maybe_add_curl above. macOS needs no extra lib (ndbm ships in libc).

static void maybe_add_ndbm(Builder *ctx, BuildTarget *t) {
    const char *v = GetEnv(ctx, "CCCC_HAS_NDBM");
    if (!v || strcmp(v, "0") == 0)
        return;
    AddDefine(t, "CCCC_HAS_NDBM", "1");
    if (strcmp(BuildHost(ctx), "linux") == 0)
        AddLib(t, "gdbm_compat");
}

// ---- Vendored libbacktrace (src/backtrace/) -------------------------------
// Compiled with distinct flags: no -std=c23 (sources are C99/C11), separate
// warning suppressions, and platform-specific format reader.
//
// CCCC_HAS_BACKTRACE=0 opt-out (#850): returns NULL instead of declaring the
// "backtrace" target at all. add_cccc_flags() already treats a NULL `bt` as
// "no libbacktrace" (skips the CCCC_HAS_BACKTRACE define, the include path,
// and the LinkWith) -- every one of the ~25 call sites that thread `bt`
// through was already written against that contract from #842 onward, so
// this opt-out needed no changes anywhere else.

// Named variant (#850): lets a graph that needs two libbacktrace archives at
// once avoid the duplicate-target-name check (#842 Step 1).
// make_libbacktrace(ctx) below is the common case: same body, fixed name.
static BuildTarget *make_libbacktrace_named(Builder *ctx, const char *name) {
    const char *has_bt = GetEnv(ctx, "CCCC_HAS_BACKTRACE");
    if (has_bt && strcmp(has_bt, "0") == 0)
        return NULL;
    BuildTarget *bt = StaticLib(ctx, name);
    AddCFlag(bt, "-O2");
    AddCFlag(bt, "-g");
    AddCFlag(bt, "-std=c11");
    AddCFlag(bt, "-Wno-unused-parameter");
    AddCFlag(bt, "-Wno-unused-variable");
    AddCFlag(bt, "-Wno-missing-field-initializers");
    AddCFlag(bt, "-Wno-shift-count-overflow");
    AddCFlag(bt, "-Wno-implicit-function-declaration");
    AddCFlag(bt, "-Wno-deprecated-declarations");
    if (strcmp(BuildHost(ctx), "linux") == 0)
        // elf.c's phdr_callback() needs the full (non-forward-declared)
        // struct dl_phdr_info from <link.h>, only visible under
        // _GNU_SOURCE on glibc. Without this every build.c target that
        // links libbacktrace fails to compile on Linux (Makefile:176 has
        // always had this; build.c never did until #842 Step 5 actually
        // exercised a Linux build).
        AddDefine(bt, "_GNU_SOURCE", (const char *)0);
    AddInclude(bt, "src/backtrace");
    // Common sources (platform-independent)
    AddSource(bt, "src/backtrace/backtrace.c");
    AddSource(bt, "src/backtrace/atomic.c");
    AddSource(bt, "src/backtrace/dwarf.c");
    AddSource(bt, "src/backtrace/fileline.c");
    AddSource(bt, "src/backtrace/mmap.c");
    AddSource(bt, "src/backtrace/mmapio.c");
    AddSource(bt, "src/backtrace/posix.c");
    AddSource(bt, "src/backtrace/print.c");
    AddSource(bt, "src/backtrace/simple.c");
    AddSource(bt, "src/backtrace/sort.c");
    AddSource(bt, "src/backtrace/state.c");
    // Platform-specific debug format reader
    if (strcmp(BuildHost(ctx), "darwin") == 0)
        AddSource(bt, "src/backtrace/macho.c");
    else
        AddSource(bt, "src/backtrace/elf.c");
    return bt;
}

static BuildTarget *make_libbacktrace(Builder *ctx) {
    return make_libbacktrace_named(ctx, "backtrace");
}

// ---- Common compile + link flags for all cccc-family targets --------------
//
// add_cccc_flags_opt() takes an explicit host-compiler optimization flag so
// callers can pick a build mode:
//   - "-O0" (debug, the default): safety checks + fast iteration, what every
//     existing target used before build modes existed (#883).
//   - "-O2" (release): what tools/release.sh and the `release` target below
//     build with, plus -DNDEBUG. This is the *host* compiler optimizing the
//     cccc binary itself -- unrelated to CCCC's own guest-side `--optimize`
//     bytecode passes or the `-0`/`-1`/`-2`/`-3` guest safety levels, which
//     this does not change.
//   - "-O1"/"-O3" (opt_test_* targets below): not shipped, exist only to run
//     the test suite against cccc built at those levels, to catch UB the
//     interpreter gets away with at -O0 (aliasing, uninitialized reads,
//     signed overflow) that only misbehaves once the host compiler starts
//     optimizing across it.
// -g is kept in every mode, including release: symbols are stripped at
// packaging time (see tools/release.sh), not compile time, so a release
// binary a user hands back a crash report from is still debuggable.

static void add_cccc_flags_opt(Builder *ctx, BuildTarget *t, BuildTarget *bt,
                               const char *opt_flag) {
    AddCFlag(t, "-Wall");
    AddCFlag(t, opt_flag);
    AddCFlag(t, "-g");
    AddCFlag(t, "-std=c23");
    AddCFlag(t, "-Wno-deprecated-declarations");
    AddCFlag(t, "-Wno-switch");
    AddCFlag(t, "-pthread");
    AddLdFlag(t, "-pthread");
    if (strcmp(BuildHost(ctx), "linux") == 0) {
        AddDefine(t, "_DEFAULT_SOURCE", (const char *)0);
        AddDefine(t, "_POSIX_C_SOURCE", "200809L");
        AddLib(t, "m");
        // A -fgnu89-inline flag used to live here, worked around a
        // "multiple definition" link failure at -O1+ for glibc's
        // extern-inline pthread.h/wchar.h functions (pthread_equal, btowc,
        // wctob, mbrlen) that was misdiagnosed as clang miscompiling them as
        // strong symbols. The real cause (#1199/#1200) was src/internal.h's
        // own #define __attribute__(x) strip: its #ifndef __attribute__
        // guard was vacuous (__attribute__ is a keyword, never a predefined
        // macro), so the strip fired unconditionally, including under
        // clang, deleting the __gnu_inline__ out of glibc's own
        // extern-inline machinery and leaving a bare extern inline -- a C99
        // *external* definition. Fixed at the source by guarding the strip
        // to !defined(__GNUC__) && !defined(__clang__); verified (#1200)
        // that removing -fgnu89-inline no longer reproduces the failure --
        // a full -O2 clang build of every real source file links clean
        // without it.
    } else if (strcmp(BuildHost(ctx), "darwin") == 0) {
        // iconv() is declared in libSystem's <iconv.h> but the symbols only
        // resolve at link time via libiconv (Makefile:97-101, verified there:
        // link fails without it, succeeds with it). glibc ships iconv in
        // libc itself, so no extra flag is needed on Linux. Without this,
        // every cccc-family target fails to link on macOS with undefined
        // _iconv/_iconv_open/_iconv_close symbols from posix.c.
        AddLib(t, "iconv");
    }
    probe_libffi(ctx, t);
    maybe_add_curl(ctx, t);
    maybe_add_decimal(ctx, t);
    maybe_add_ndbm(ctx, t);
    if (bt) {
        AddDefine(t, "CCCC_HAS_BACKTRACE", "1");
        AddInclude(t, "src/backtrace");
        LinkWith(t, bt);
    }
    // Git describe stamping for --version (#883). Absent in release
    // tarballs (no .git) -- CCCC_GIT_DESC then falls back to "" (see
    // src/internal.h) and --version shows CCCC_RELEASE_VERSION alone.
    if (FileExists(ctx, ".git")) {
        const char *desc = CaptureCommand(
            ctx, "git describe --tags --always --dirty 2>/dev/null");
        if (desc && *desc) {
            char quoted[256];
            snprintf(quoted, sizeof(quoted), "\"%s\"", desc);
            AddDefine(t, "CCCC_GIT_DESC", quoted);
        }
    }
}

// Debug mode (the default everywhere except release/opt_test_* below).
static void add_cccc_flags(Builder *ctx, BuildTarget *t, BuildTarget *bt) {
    add_cccc_flags_opt(ctx, t, bt, "-O0");
}

// ---- Source glob shared by all cccc executable targets -------------------

static void add_cccc_sources(Builder *ctx, BuildTarget *t) {
    AddSourcesGlob(t, "src/*.c");
    AddSourcesGlob(t, "src/stdlib/*.c");
    ExcludeSource(t, "src/ops.c");
    // src/std.c (generated) and src/std_stub.c (committed stage0 stand-in)
    // both define get_std_header/get_stdlib_reg_fn_name/get_std_header_name
    // -- never compile both into the same target. std.c wins whenever it
    // exists on disk; same self-correcting rule the Makefile's SRCS uses.
    if (FileExists(ctx, "src/std.c"))
        ExcludeSource(t, "src/std_stub.c");
    else
        ExcludeSource(t, "src/std.c"); // no-op when absent; harmless
}

// ---- Reusable cccc executable factory ------------------------------------

// opt_flag/ndebug let release() and the opt_test_* targets below reuse this
// without duplicating add_cccc_sources()'s exclusion rules.
static BuildTarget *make_cccc_exe_named_opt(Builder *ctx, BuildTarget *bt,
                                            const char *name,
                                            const char *opt_flag, bool ndebug) {
    BuildTarget *t = Executable(ctx, name);
    SetOutput(t, name);
    add_cccc_sources(ctx, t);
    add_cccc_flags_opt(ctx, t, bt, opt_flag);
    if (ndebug)
        AddDefine(t, "NDEBUG", (const char *)0);
    return t;
}

static BuildTarget *make_cccc_exe_named(Builder *ctx, BuildTarget *bt,
                                        const char *name) {
    return make_cccc_exe_named_opt(ctx, bt, name, "-O0", false);
}

// ---- [[cccc::build_target]] factories (invoked via --build-target=NAME) --

// Shared by the four cccc_* sanitizer factories below and by sanitizers()
// (which must build all four in ONE graph, so it passes in a single shared
// `bt` -- calling make_libbacktrace() once per factory, as each did
// standalone, would declare four "backtrace" targets in the same graph and
// trip the duplicate-name check added in #842 Step 1).
static BuildTarget *make_sanitizer_variant(Builder *ctx, BuildTarget *bt,
                                           const char *name,
                                           const char *sanitize_flag) {
    BuildTarget *t = Executable(ctx, name);
    SetOutput(t, name);
    add_cccc_sources(ctx, t);
    add_cccc_flags(ctx, t, bt);
    AddCFlag(t, sanitize_flag);
    AddLdFlag(t, sanitize_flag);
    return t;
}

[[cccc::build_target]]
BuildTarget *cccc_asan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_sanitizer_variant(ctx, bt, "cccc-asan",
                                  "-fsanitize=address,undefined");
}

[[cccc::build_target]]
BuildTarget *cccc_ubsan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_sanitizer_variant(ctx, bt, "cccc-ubsan",
                                  "-fsanitize=undefined");
}

[[cccc::build_target]]
BuildTarget *cccc_tsan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_sanitizer_variant(ctx, bt, "cccc-tsan", "-fsanitize=thread");
}

[[cccc::build_target]]
BuildTarget *cccc_msan(Builder *ctx) {
    // MemorySanitizer requires Linux + clang; will fail to link on macOS
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_sanitizer_variant(ctx, bt, "cccc-msan", "-fsanitize=memory");
}

[[cccc::build_target]]
BuildTarget *fuzz_harness(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t  = Executable(ctx, "fuzz_harness");
    SetOutput(t, "fuzz_harness");
    add_cccc_sources(ctx, t);
    ExcludeSource(t, "src/main.c");
    add_cccc_flags(ctx, t, bt);
    AddCFlag(t, "-fsanitize=fuzzer,address");
    AddLdFlag(t, "-fsanitize=fuzzer,address");
    return t;
}

[[cccc::build_target]]
BuildTarget *libcccc(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t  = DynamicLib(ctx, "cccc"); // -> lib/libcccc.{dylib,so}
    add_cccc_sources(ctx, t);
    ExcludeSource(t, "src/main.c");            // no main() in a shared library
    add_cccc_flags(ctx, t, bt);
    AddCFlag(t, "-fPIC");
    return t;
}

// ---- release build mode (#883) ---------------------------------------------
// -O2 -g -DNDEBUG, as a first-class target rather than an ad-hoc flag
// override -- what tools/release.sh builds and packages. Output is named
// cccc-release (not cccc) so it never collides with a debug build sitting in
// the same out_dir.

// #1144: forward declaration -- release() below calls reflection_ffi_gen()
// (defined further down, line ~678) before its own definition is in scope.
// This used to compile via implicit function declaration, silently
// tolerated at every standard; that leniency is now a hard error at C99+
// (CCCC's own default), so an explicit prototype is required like any
// other forward reference.
BuildTarget *reflection_ffi_gen(Builder *ctx);
BuildTarget *shims_gen(Builder *ctx); // same forward-reference reason as above

[[cccc::build_target]]
BuildTarget *release(Builder *ctx) {
    BuildTarget *bt             = make_libbacktrace(ctx);
    BuildTarget *gen            = stdlib_regen_step(ctx, bt);
    BuildTarget *reflection_gen = reflection_ffi_gen(ctx);
    BuildTarget *shims_gen_step = shims_gen(ctx);
    BuildTarget *final =
        make_cccc_exe_named_opt(ctx, bt, "cccc-release", "-O2", true);
    DependsOn(final, gen);
    DependsOn(final, reflection_gen);
    DependsOn(final, shims_gen_step);
    return final;
}

// ---- opt_test_O1 / O2 / O3 (#883) ------------------------------------------
// Build cccc at a given host optimization level and run the full suite
// against it. Not shipped -- these exist purely to catch UB the interpreter
// gets away with at -O0 (aliasing, uninitialized reads, signed overflow)
// that only misbehaves once the host compiler starts optimizing across it.

static BuildTarget *make_opt_test(Builder *ctx, const char *level,
                                  const char *opt_flag) {
    BuildTarget *bt = make_libbacktrace(ctx);
    char         exe_name[32];
    snprintf(exe_name, sizeof(exe_name), "cccc-opt-%s", level);
    BuildTarget *cccc =
        make_cccc_exe_named_opt(ctx, bt, exe_name, opt_flag, false);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "python3 tools/run_tests.py --binary %s",
             TargetOutput(cccc));
    char step_name[32];
    snprintf(step_name, sizeof(step_name), "opt-test-%s", level);
    BuildTarget *step = RunCustom(ctx, step_name, cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *opt_test_O1(Builder *ctx) {
    return make_opt_test(ctx, "O1", "-O1");
}

[[cccc::build_target]]
BuildTarget *opt_test_O2(Builder *ctx) {
    return make_opt_test(ctx, "O2", "-O2");
}

[[cccc::build_target]]
BuildTarget *opt_test_O3(Builder *ctx) {
    return make_opt_test(ctx, "O3", "-O3");
}

// ---- Sanitizer aggregate (Makefile:217-227) --------------------------------
// No meta-target concept exists in the builder API; a no-op RunCustom that
// DependsOn's the four (three on macOS: msan needs Linux+clang) sanitizer
// targets is the cheapest correct shape.

[[cccc::build_target]]
BuildTarget *sanitizers(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *asan = make_sanitizer_variant(ctx, bt, "cccc-asan",
                                               "-fsanitize=address,undefined");
    BuildTarget *ubsan =
        make_sanitizer_variant(ctx, bt, "cccc-ubsan", "-fsanitize=undefined");
    BuildTarget *tsan =
        make_sanitizer_variant(ctx, bt, "cccc-tsan", "-fsanitize=thread");
    BuildTarget *step = RunCustom(ctx, "sanitizers", "true");
    DependsOn(step, asan);
    DependsOn(step, ubsan);
    DependsOn(step, tsan);
    if (strcmp(BuildHost(ctx), "linux") == 0)
        DependsOn(step, make_sanitizer_variant(ctx, bt, "cccc-msan",
                                               "-fsanitize=memory"));
    return step;
}

// ---- clean (Makefile:613-618) ----------------------------------------------
// All build.c-produced artifacts live under out_dir (build/ by default), so
// deleting it covers everything the Makefile itemizes individually
// (EXE_OUT/LIB_OUT/SAN_OUT/cccc-afl*/cccc-prof/fuzz_harness/host_tests/
// libbacktrace). fuzz/ and profile/*.{prof,txt,json,massif} live outside
// out_dir (shared with the Makefile's own fuzz-* / profile-* targets) so are
// cleaned explicitly too. Deletions run as side effects of calling this
// factory; the returned target is a no-op placeholder for run_graph.

[[cccc::build_target]]
BuildTarget *clean(Builder *ctx) {
    DeleteDir(ctx, BuildOutDir(ctx));
    DeleteDir(ctx, "fuzz/seeds");
    DeleteDir(ctx, "fuzz/out");
    DeleteDir(ctx, "profile/vm-opcodes");
    DeleteDir(ctx, "profile/bench-results");
    DeleteDir(ctx, "profile/perf");
    DeleteFile(ctx, "profile/bench.json");
    DeleteFile(ctx, "profile/cpu.prof");
    DeleteFile(ctx, "profile/mem-leaks.txt");
    DeleteFile(ctx, "profile/mem.massif");
    return RunCustom(ctx, "clean", "true");
}

// ---- host-tests (Makefile:261-279, #707) -----------------------------------
// Links the compiler sources directly into each tests/host/test_*.c,
// bypassing tools/tests.py's guest-.c/exit-42 protocol, for tests that need
// multiple VirtualMachines or threads in one host process.
//
// Simplification vs the Makefile recipe: the binaries are run as a single
// `&&`-chained RunCustom command (fails fast on the first failure) rather
// than "run every binary, then fail if any of them failed" -- the vendored
// RunCustom shell has no variables or exit-code capture to accumulate a
// fail flag across commands the way the Makefile's `for` loop does.

[[cccc::build_target]]
BuildTarget *host_tests(Builder *ctx) {
    BuildTarget *bt    = make_libbacktrace(ctx);
    const char **files = GlobFiles(ctx, "tests/host/test_*.c");
    BuildTarget *tests[256];
    int          n = 0;
    char         cmd[8192];
    cmd[0] = '\0';
    for (int i = 0; files[i] && n < 256; i++) {
        const char *path  = files[i];
        const char *slash = strrchr(path, '/');
        const char *base  = slash ? slash + 1 : path;
        char        name[128];
        snprintf(name, sizeof(name), "host_%s", base);
        size_t nlen = strlen(name);
        if (nlen > 2 && strcmp(name + nlen - 2, ".c") == 0)
            name[nlen - 2] = '\0';

        BuildTarget *t = Executable(ctx, name);
        SetOutput(t, name);
        AddSource(t, path);
        AddInclude(t, "src");
        add_cccc_sources(ctx, t);
        ExcludeSource(t, "src/main.c");
        add_cccc_flags(ctx, t, bt);

        if (n > 0)
            strncat(cmd, " && ", sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, TargetOutput(t), sizeof(cmd) - strlen(cmd) - 1);
        tests[n++] = t;
    }
    if (n == 0) {
        fprintf(stderr,
                "build: host_tests found no tests/host/test_*.c files\n");
        return RunCustom(ctx, "host-tests", "true");
    }
    BuildTarget *step = RunCustom(ctx, "host-tests", cmd);
    for (int i = 0; i < n; i++)
        DependsOn(step, tests[i]);
    return step;
}

// ---- test / test-suites / test-legacy / sqlite-smoke / audit-ffi
// (Makefile:368-389) --

[[cccc::build_target]]
BuildTarget *test(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char         cmd[512];
    snprintf(cmd, sizeof(cmd), "python3 tools/run_tests.py --binary %s",
             TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "test", cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *test_suites(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char         cmd[512];
    snprintf(cmd, sizeof(cmd), "python3 tools/tests.py --suites --binary %s",
             TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "test-suites", cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *test_legacy(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char         cmd[512];
    snprintf(cmd, sizeof(cmd), "python3 tools/tests.py --legacy --binary %s",
             TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "test-legacy", cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *sqlite_smoke(Builder *ctx) {
    // tools/sqlite_smoke.py hardcodes root/"cccc" -- it takes no --binary
    // override, unlike tools/tests.py and tools/run_tests.py. Put the built
    // binary there via `cp` + atomic `mv`, NOT a direct `cp` onto ./cccc:
    // when this whole build.c invocation is itself running as ./cccc (the
    // common case -- ./cccc --build build.c), an in-place `cp` truncates
    // and rewrites the very inode the running process's text segment is
    // demand-paged from, and it gets SIGKILLed the next time the kernel
    // faults in a code page. `mv` swaps the directory entry to a new inode
    // atomically, leaving the running process's already-open mapping of
    // the old one untouched until it exits.
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char         cmd[512];
    snprintf(
        cmd, sizeof(cmd),
        "cp %s ./cccc.sqlite-smoke-tmp && mv ./cccc.sqlite-smoke-tmp ./cccc "
        "&& python3 tools/sqlite_smoke.py",
        TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "sqlite-smoke", cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *header_resolution_smoke(Builder *ctx) {
    // tools/header_resolution_smoke.py (#891) hardcodes root/"cccc", same as
    // sqlite_smoke.py above -- see the comment on sqlite_smoke for why the
    // built binary is placed via `cp` + atomic `mv` rather than a direct
    // `cp` onto ./cccc.
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char         cmd[512];
    snprintf(cmd, sizeof(cmd),
             "cp %s ./cccc.header-resolution-smoke-tmp && "
             "mv ./cccc.header-resolution-smoke-tmp ./cccc "
             "&& python3 tools/header_resolution_smoke.py",
             TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "header-resolution-smoke", cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *cli_exit_code_smoke(Builder *ctx) {
    // tools/cli_exit_code_smoke.py (#1260) hardcodes root/"cccc", same as
    // header_resolution_smoke above -- see the comment on sqlite_smoke for
    // why the built binary is placed via `cp` + atomic `mv` rather than a
    // direct `cp` onto ./cccc. Also run inside the unified `test` target via
    // run_tests.py; this standalone target is for running it in isolation.
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char         cmd[512];
    snprintf(cmd, sizeof(cmd),
             "cp %s ./cccc.cli-exit-code-smoke-tmp && "
             "mv ./cccc.cli-exit-code-smoke-tmp ./cccc "
             "&& python3 tools/cli_exit_code_smoke.py",
             TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "cli-exit-code-smoke", cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *comptime_native_smoke(Builder *ctx) {
    // tools/comptime_native_smoke.py (#892) hardcodes root/"cccc", same as
    // header_resolution_smoke above -- see the comment on sqlite_smoke for
    // why the built binary is placed via `cp` + atomic `mv` rather than a
    // direct `cp` onto ./cccc.
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char         cmd[512];
    snprintf(cmd, sizeof(cmd),
             "cp %s ./cccc.comptime-native-smoke-tmp && "
             "mv ./cccc.comptime-native-smoke-tmp ./cccc "
             "&& python3 tools/comptime_native_smoke.py",
             TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "comptime-native-smoke", cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *audit_ffi(Builder *ctx) {
    // Pure source scan (src/stdlib/*.c FFI registrations vs include/**/*.h
    // declarations); no build required (Makefile:384-388).
    return RunCustom(ctx, "audit-ffi", "python3 tools/audit_ffi.py");
}

[[cccc::build_target]]
BuildTarget *audit_reflection_enums(Builder *ctx) {
    // Pure source scan: include/cccc/reflection.h's TypeKind (TK_*) /
    // NodeKind (NK_*) / AttrTargetKind hand-copied enums vs the compiler's
    // internal TypeKind (TY_*) / NodeKind (ND_*) / AttrTargetKind in
    // src/cccc.h -- no build required, same reasoning as audit_ffi above.
    return RunCustom(ctx, "audit-reflection-enums",
                     "python3 tools/audit_reflection_enums.py");
}

// src/reflection_ffi_protos.inc / src/reflection_ffi_register.inc are
// generated from include/cccc/reflection.h by tools/gen_reflection_ffi.py
// and #include'd by both src/macros.c and src/reflection.c. Unlike
// src/std.c, they're committed (see the header of gen_reflection_ffi.py for
// why) so this step -- pure Python, no cccc binary needed -- keeps them
// fresh on every default build; reflection_ffi_check below (wired into the
// test target) catches staleness for any build that skips it.
//
// AddInput (#851) declares the real dependency (the source header + the
// generator script itself) so build_target() can skip re-running the
// generator when neither has changed since the .inc files were last written.
[[cccc::build_target]]
BuildTarget *reflection_ffi_gen(Builder *ctx) {
    BuildTarget *gen = RunCustom(ctx, "reflection-ffi-gen",
                                 "python3 tools/gen_reflection_ffi.py");
    DeclareOutput(gen, "src/reflection_ffi_protos.inc");
    DeclareOutput(gen, "src/reflection_ffi_register.inc");
    AddInput(gen, "include/cccc/reflection.h");
    AddInput(gen, "tools/gen_reflection_ffi.py");
    return gen;
}

[[cccc::build_target]]
BuildTarget *reflection_ffi_check(Builder *ctx) {
    return RunCustom(ctx, "reflection-ffi-check",
                     "python3 tools/gen_reflection_ffi.py --check");
}

// src/shims.inc is the -c=native support-shim text table, generated from the
// ordinary C under src/shims/ by tools/gen_shims.py and #include'd by
// src/serialize_shims.c. Committed (like reflection_ffi_*.inc, unlike
// src/std.c) so plain `make` needs no python3; this step keeps it fresh on
// every default build, shims_check below (wired into the test target)
// catches staleness for any build that skips it. AddInput declares the real
// dependency -- every src/shims/*.c plus the generator itself.
[[cccc::build_target]]
BuildTarget *shims_gen(Builder *ctx) {
    BuildTarget *gen =
        RunCustom(ctx, "shims-gen", "python3 tools/gen_shims.py");
    DeclareOutput(gen, "src/shims.inc");
    AddInput(gen, "tools/gen_shims.py");
    const char **shims = GlobFiles(ctx, "src/shims/*.c");
    for (int i = 0; shims && shims[i]; i++)
        AddInput(gen, shims[i]);
    return gen;
}

[[cccc::build_target]]
BuildTarget *shims_check(Builder *ctx) {
    return RunCustom(ctx, "shims-check", "python3 tools/gen_shims.py --check");
}

// Doxygen HTML API docs for the three public headers.
// Pure source scan -- no cccc binary needed, same reasoning as audit_ffi
// above -- but doxygen is an external dependency, so it's guarded with
// HaveTool the way bench() guards on hyperfine. Output is gitignored
// (build/docs/) and never committed; not part of build_main() so the
// default build stays dependency-free.
[[cccc::build_target]]
BuildTarget *docs(Builder *ctx) {
    if (!HaveTool(ctx, "doxygen")) {
        fprintf(
            stderr,
            "build: docs requires doxygen (not found in PATH) — "
            "install it (e.g. brew install doxygen / apt install doxygen)\n");
        return RunCustom(ctx, "docs", "false");
    }
    BuildTarget *step =
        RunCustom(ctx, "docs", "mkdir -p build/docs && doxygen Doxyfile");
    AddInput(step, "Doxyfile");
    AddInput(step, "include/cccc/building.h");
    AddInput(step, "include/cccc/reflection.h");
    AddInput(step, "include/cccc/testing.h");
    DeclareOutput(step, "build/docs/html/index.html");
    return step;
}

// ---- bench-compare{,-quick,-json} (Makefile:551-564) -----------------------

static BuildTarget *make_bench_compare(Builder *ctx, const char *name,
                                       const char *pyflags) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char         cmd[512];
    snprintf(cmd, sizeof(cmd), "python3 tools/bench.py %s", pyflags);
    BuildTarget *step = RunCustom(ctx, name, cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *bench_compare(Builder *ctx) {
    return make_bench_compare(ctx, "bench-compare", "--runs 3 --warmup 1");
}

[[cccc::build_target]]
BuildTarget *bench_compare_quick(Builder *ctx) {
    return make_bench_compare(ctx, "bench-compare-quick",
                              "--runs 2 --warmup 1");
}

[[cccc::build_target]]
BuildTarget *bench_compare_json(Builder *ctx) {
    return make_bench_compare(ctx, "bench-compare-json",
                              "--format json --runs 3 --warmup 1");
}

// ---- profile-cpu / profile-mem (Makefile:566-599) --------------------------

[[cccc::build_target]]
BuildTarget *profile_cpu(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t  = Executable(ctx, "cccc-prof");
    SetOutput(t, "cccc-prof");
    add_cccc_sources(ctx, t);
    add_cccc_flags(ctx, t, bt);
    if (strcmp(BuildHost(ctx), "darwin") == 0)
        AddLibPath(t, "/opt/homebrew/lib");
    AddLib(t, "profiler");
    // The vendored RunCustom shell has no `VAR=value cmd` env-prefix syntax
    // (a real POSIX shell feature; ours doesn't parse it), so `env` does the
    // env-var assignment instead -- `env VAR=value cmd args...` is a single
    // argv[0]="env" invocation the shell's plain command parser handles fine.
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "mkdir -p profile && env CPUPROFILE=profile/cpu.prof %s "
             "-I./include tests/benchmarks/mandelbrot.c || true",
             TargetOutput(t));
    BuildTarget *step = RunCustom(ctx, "profile-cpu", cmd);
    DependsOn(step, t);
    return step;
}

[[cccc::build_target]]
BuildTarget *profile_mem(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char         cmd[512];
    if (strcmp(BuildHost(ctx), "darwin") == 0)
        snprintf(cmd, sizeof(cmd),
                 "mkdir -p profile && leaks -atExit -- %s -I./include "
                 "tests/benchmarks/mandelbrot.c "
                 "> profile/mem-leaks.txt 2>&1 || true",
                 TargetOutput(cccc));
    else
        snprintf(cmd, sizeof(cmd),
                 "mkdir -p profile && valgrind --tool=massif "
                 "--massif-out-file=profile/mem.massif "
                 "%s -I./include tests/benchmarks/mandelbrot.c || true",
                 TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "profile-mem", cmd);
    DependsOn(step, cccc);
    return step;
}

// ---- dsym (Makefile:608-617, macOS-only) -----------------------------------

[[cccc::build_target]]
BuildTarget *dsym(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    BuildTarget *step;
    if (strcmp(BuildHost(ctx), "darwin") == 0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "dsymutil %s", TargetOutput(cccc));
        step = RunCustom(ctx, "dsym", cmd);
    } else {
        step = RunCustom(ctx, "dsym",
                         "echo 'dsym: no-op on this platform (DWARF is already "
                         "in the ELF)'");
    }
    DependsOn(step, cccc);
    return step;
}

// ---- afl / afl-asan (Makefile:229-252) -------------------------------------
// AFL++ compiler wrappers are found via FindTool (subject to
// --build-tool-allow) rather than PATH-searched by the shell directly, so
// the tool-allowlist gate that already governs HaveTool/PkgConfig/
// CaptureCommand applies here too.

static const char *find_afl_cc(Builder *ctx) {
    const char *cc = FindTool(ctx, "afl-clang-fast");
    if (cc)
        return cc;
    return FindTool(ctx, "afl-clang");
}

[[cccc::build_target]]
BuildTarget *afl(Builder *ctx) {
    const char *afl_cc = find_afl_cc(ctx);
    if (!afl_cc) {
        fprintf(stderr, "build: afl requires afl-clang-fast or afl-clang "
                        "(AFL++ not found)\n");
        return RunCustom(ctx, "afl", "false");
    }
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t  = Executable(ctx, "cccc-afl");
    SetOutput(t, "cccc-afl");
    add_cccc_sources(ctx, t);
    add_cccc_flags(ctx, t, bt);
    SetToolchain(t, afl_cc);
    return t;
}

[[cccc::build_target]]
BuildTarget *afl_asan(Builder *ctx) {
    const char *afl_cc = find_afl_cc(ctx);
    if (!afl_cc) {
        fprintf(stderr, "build: afl-asan requires afl-clang-fast or afl-clang "
                        "(AFL++ not found)\n");
        return RunCustom(ctx, "afl-asan", "false");
    }
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t  = Executable(ctx, "cccc-afl-asan");
    SetOutput(t, "cccc-afl-asan");
    add_cccc_sources(ctx, t);
    add_cccc_flags(ctx, t, bt);
    SetToolchain(t, afl_cc);
    SetTargetEnv(t, "AFL_USE_ASAN", "1");
    return t;
}

// #1198 regression guard: reusing the same --build-cache across two DIFFERENT
// COMPILER FAMILIES (not architectures) must not serve the wrong family's
// objects to the link step off the Level 1 mtime fast path. Builds
// tests/test_build_cache.c into a shared --build-cache dir three times --
// default compiler, same compiler again (must cache-hit), then
// --build-cc=<other family> (must NOT cache-hit, i.e. must actually
// recompile) -- via a single freshly-built "cccc" so the smoke isn't at the
// mercy of a possibly-stale repo-root ./cccc. Skips cleanly (not a failure)
// if only one compiler family is reachable on PATH.
[[cccc::build_target]]
BuildTarget *build_cache_toolchain_smoke(Builder *ctx) {
    const char *other_cc = CaptureCommand(
        ctx, "sh -c 'if cc --version 2>/dev/null | grep -qi clang; then "
             "command -v gcc-16 || command -v gcc-15 || command -v gcc-14 "
             "|| command -v gcc-13 || command -v gcc; "
             "else command -v clang; fi'");
    if (!other_cc || !*other_cc) {
        fprintf(stderr, "build: build_cache_toolchain_smoke skipped (only "
                        "one compiler family on PATH)\n");
        return RunCustom(ctx, "build-cache-toolchain-smoke", "true");
    }

    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");

    // The vendored RunCustom shell (build_shell.c) has no `!`/`$?` -- so
    // "did NOT cache-hit" is checked positively instead: a real recompile
    // under the other compiler prints that compiler's own path in its
    // command line (the vendored shell's "(cached)"/"(up to date)" lines
    // never do), so grep FOR other_cc rather than grep -v/! for "(cached)".
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "rm -rf build/build_cache_toolchain_smoke && "
             "%s -I./include --build "
             "--build-out-dir=build/build_cache_toolchain_smoke --build-cache "
             "tests/test_build_cache.c >/dev/null && "
             "%s -I./include --build "
             "--build-out-dir=build/build_cache_toolchain_smoke --build-cache "
             "tests/test_build_cache.c | grep -q '(cached)' && "
             "%s -I./include --build "
             "--build-out-dir=build/build_cache_toolchain_smoke --build-cache "
             "--build-cc=%s tests/test_build_cache.c | grep -q -- '%s' && "
             "rm -rf build/build_cache_toolchain_smoke && "
             "echo 'build-cache-toolchain-smoke: OK'",
             TargetOutput(cccc), TargetOutput(cccc), TargetOutput(cccc),
             other_cc, other_cc);
    BuildTarget *step = RunCustom(ctx, "build-cache-toolchain-smoke", cmd);
    DependsOn(step, cccc);
    return step;
}

// ---- Linux via Colima (Makefile:458-556) -----------------------------------
// Orchestration around `colima -p <profile> nerdctl -- run ...`, run from
// the host (these do not build anything themselves -- the container image
// already has its own toolchain, built from the repo's own Dockerfile).
// #850 splits the previous single-shot linux_amd64_test/linux_aarch64_test
// into the Makefile's staged -build/-smoke/-test workflow, plus the
// amd64-only 5-way sharded test run and the MSan in-container test target.
// The smoke/sharded-test recipes need $(...) substitution, $? capture, and
// (for the sharded test) a `for` loop with a fail-flag accumulator -- none
// of which the vendored build shell supports -- so they're delegated to
// real shell scripts (tools/linux_container_smoke.sh,
// tools/linux_amd64_test.sh, tools/linux_amd64_msan_test.sh) rather than
// inlined into RunCustom strings.

[[cccc::build_target]]
BuildTarget *linux_amd64_build(Builder *ctx) {
    return RunCustom(
        ctx, "linux-amd64-build",
        "colima -p cccc-linux-amd64 nerdctl -- build --platform linux/amd64 "
        "-t cccc-linux-amd64 .");
}

[[cccc::build_target]]
BuildTarget *linux_aarch64_build(Builder *ctx) {
    return RunCustom(
        ctx, "linux-aarch64-build",
        "colima -p cccc-linux-arm64 nerdctl -- build --platform linux/arm64 "
        "-t cccc-linux-arm64 .");
}

[[cccc::build_target]]
BuildTarget *linux_amd64_smoke(Builder *ctx) {
    BuildTarget *step = RunCustom(
        ctx, "linux-amd64-smoke",
        "sh tools/linux_container_smoke.sh cccc-linux-amd64 cccc-linux-amd64 "
        "linux/amd64 x86_64 'x86-64|x86_64'");
    DependsOn(step, linux_amd64_build(ctx));
    return step;
}

[[cccc::build_target]]
BuildTarget *linux_aarch64_smoke(Builder *ctx) {
    BuildTarget *step = RunCustom(
        ctx, "linux-aarch64-smoke",
        "sh tools/linux_container_smoke.sh cccc-linux-arm64 cccc-linux-arm64 "
        "linux/arm64 aarch64 'aarch64|arm64'");
    DependsOn(step, linux_aarch64_build(ctx));
    return step;
}

[[cccc::build_target]]
BuildTarget *linux_amd64_test(Builder *ctx) {
    BuildTarget *step = RunCustom(
        ctx, "linux-amd64-test",
        "sh tools/linux_amd64_test.sh cccc-linux-amd64 cccc-linux-amd64 8");
    DependsOn(step, linux_amd64_smoke(ctx));
    return step;
}

[[cccc::build_target]]
BuildTarget *linux_aarch64_test(Builder *ctx) {
    // No 5-way sharding on this side (Makefile:552-555 was always
    // single-shot here too -- only the x86_64/amd64 side shards).
    BuildTarget *step = RunCustom(
        ctx, "linux-aarch64-test",
        "colima -p cccc-linux-arm64 nerdctl -- run --rm --platform linux/arm64 "
        "cccc-linux-arm64 timeout 600 python3 tools/run_tests.py -j 8");
    DependsOn(step, linux_aarch64_smoke(ctx));
    return step;
}

// MSan build + full in-container test run (Makefile:523-526). Expected to
// report failures: an uninstrumented libc/libffi MSan blind spot
// (documented in man/TESTING.md, #844) accounts for ~262/700 of them --
// not a regression on its own, compare against that documented baseline.
[[cccc::build_target]]
BuildTarget *linux_amd64_msan_test(Builder *ctx) {
    BuildTarget *step = RunCustom(ctx, "linux-amd64-msan-test",
                                  "sh tools/linux_amd64_msan_test.sh "
                                  "cccc-linux-amd64 cccc-linux-amd64 8");
    DependsOn(step, linux_amd64_build(ctx));
    return step;
}

// ---- Two-pass stdlib regeneration (#571/#842) ------------------------------
//
// Pass 1 builds "cccc-pass1" against whatever src/std.c is currently on
// disk. That compiler regenerates src/std.c from tools/generate_stdlib.c via
// tools/regen_stdlib.sh's atomic mktemp+cmp+mv recipe (a literal `> file`
// shell redirect would truncate the committed 406 KB src/std.c the instant
// the shell opens it, before the generator writes a single byte). The
// script also skips the mv entirely when the regenerated content is
// unchanged, so an already-up-to-date src/std.c keeps its mtime.
//
// Pass 2 (the caller's job — see build_main below) then (re)compiles the
// real "cccc" against the regenerated src/std.c. With no --build-cache
// enabled for this graph, every source recompiles unconditionally
// (src/build.c's incremental checks are gated on ctx->cache_dir), so pass 2
// genuinely picks up whatever pass 1 wrote.
//
// AddInput (#851) declares the generator's real inputs -- generate_stdlib.c
// itself, the regen script, and every embedded header -- so build_target()
// can report this step "up to date" and skip it when none of them changed.
// Two things this does NOT buy, worth being honest about: the step still
// DependsOn(gen, pass1), a full compiler build that recompiles
// unconditionally without --build-cache, so on the default build skipping
// the (already fast) regen script itself saves little -- reflection_ffi_gen
// above is where AddInput's win actually shows up. And regen_stdlib.sh
// deliberately preserves src/std.c's mtime on a no-change regen, so merely
// touching a header (without changing its content) makes this step re-run
// every build until the generated content actually differs -- intended, not
// a bug in either regen_stdlib.sh or this up-to-date check.
static BuildTarget *stdlib_regen_step(Builder *ctx, BuildTarget *bt) {
    BuildTarget *pass1 = make_cccc_exe_named(ctx, bt, "cccc-pass1");
    char         cmd[512];
    snprintf(cmd, sizeof(cmd), "sh tools/regen_stdlib.sh %s",
             TargetOutput(pass1));
    BuildTarget *gen = RunCustom(ctx, "stdlib-regen", cmd);
    DeclareOutput(gen, "src/std.c");
    DependsOn(gen, pass1);
    AddInput(gen, "tools/generate_stdlib.c");
    AddInput(gen, "tools/regen_stdlib.sh");
    const char **hdrs1 = GlobFiles(ctx, "include/*.h");
    for (int i = 0; hdrs1 && hdrs1[i]; i++)
        AddInput(gen, hdrs1[i]);
    const char **hdrs2 = GlobFiles(ctx, "include/*/*.h");
    for (int i = 0; hdrs2 && hdrs2[i]; i++)
        AddInput(gen, hdrs2[i]);
    return gen;
}

[[cccc::build_target]]
BuildTarget *stdlib_gen(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return stdlib_regen_step(ctx, bt);
}

[[cccc::build_target]]
BuildTarget *bench(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    if (!HaveTool(ctx, "hyperfine")) {
        fprintf(stderr, "build: bench requires hyperfine (not found in PATH) — "
                        "install it (e.g. brew install hyperfine / apt install "
                        "hyperfine)\n");
        return RunCustom(ctx, "bench", "false");
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "mkdir -p profile && hyperfine --warmup 3 --ignore-failure "
             "--export-json profile/bench.json '%s -I./include "
             "tests/benchmarks/mandelbrot.c'",
             TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "bench", cmd);
    DependsOn(step, cccc);
    return step;
}

// ---- Default build entry -------------------------------------------------
// Builds: libbacktrace (vendored), the two-pass stdlib regen, the
// reflection FFI regen, and the final cccc executable. All other targets
// are available via --build-target=NAME.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *bt             = make_libbacktrace(ctx);
    BuildTarget *gen            = stdlib_regen_step(ctx, bt);
    BuildTarget *reflection_gen = reflection_ffi_gen(ctx);
    BuildTarget *shims_gen_step = shims_gen(ctx);
    BuildTarget *final          = make_cccc_exe_named(ctx, bt, "cccc");
    DependsOn(final, gen);
    DependsOn(final, reflection_gen);
    DependsOn(final, shims_gen_step);
    return BuildAll(ctx);
}
