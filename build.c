// build.c — self-hosting build script for CCCC.
//
// The main build is `make` (see Makefile), which produces ./cccc. This script
// is the same build expressed through cccc's own --build API, and drives the
// extra targets (tests, sanitizers, fuzzing, ...):
//   ./cccc --build build.c [--build-target=NAME]
// The default build produces build/cccc.
//
// Selective builds via --build-target=NAME:
//   cccc_asan          AddressSanitizer + UBSan build  → cccc-asan
//   cccc_ubsan         UndefinedBehaviourSanitizer      → cccc-ubsan
//   cccc_tsan          ThreadSanitizer                  → cccc-tsan
//   cccc_msan          MemorySanitizer (Linux+clang)    → cccc-msan
//   sanitizers         all of the above in one graph
//   fuzz_harness       libFuzzer + ASan harness         → fuzz_harness
//   afl / afl_asan     AFL++ builds (needs afl-clang-fast/afl-clang)
//   bench              hyperfine benchmark
//   bench_compare{,_quick,_json}, bench_memory, profile_cpu, profile_mem, dsym
//   clean, host_tests (also run by test), fuzz_replay, test_smoke,
//   test_leaks,
//   test / test_vm /
//   test_native / test_release / test_msan / test_ubsan / test_asan /
//   test_tsan; test_extra (the loose tests) =
//   test_warnings / test_serialize / test_build_mode / test_safety /
//   test_parallel / test_macros / test_misc / test_native_loose (the loose
//   tests under -c=native) docs                Doxygen HTML
//   API docs for include/cccc/*.h (needs doxygen) build_cache_toolchain_smoke
//   #1198 regression guard (same-arch, different-compiler-family --build-cache
//   reuse; skips if only one compiler family is on PATH) linux_amd64_build /
//   linux_aarch64_build    build the Colima container image linux_amd64_smoke /
//   linux_aarch64_smoke    + container arch/exit-42 sanity check
//   linux_amd64_test / linux_aarch64_test      + the suites
//   linux_amd64_msan_test                      + cccc-msan build + the suites
//   (#844 known-noise)
//
// Not covered here: cross-compiling for a 4th Colima profile or other
// architectures not listed above.

#include <stdio.h>
#include <string.h>

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
// into a gitignored prefix. Mirrors the Makefile's CCCC_HAS_DECIMAL block,
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

// ---- libbacktrace (vendor/libbacktrace submodule + src/backtrace/ overlay) --
// Compiled with distinct flags: no -std=c23 (sources are C99/C11), separate
// warning suppressions, and platform-specific format reader. The hand-written
// config.h/backtrace-supported.h live in src/backtrace/; everything else comes
// from the submodule. An uninitialised submodule builds without libbacktrace,
// like CCCC_HAS_BACKTRACE=0.
//
// CCCC_HAS_BACKTRACE=0 opt-out (#850): returns NULL instead of declaring the
// "backtrace" target at all. add_cccc_flags() already treats a NULL `bt` as
// "no libbacktrace" (skips the CCCC_HAS_BACKTRACE define, the include path,
// and the LinkWith) -- every one of the ~25 call sites that thread `bt`
// through was already written against that contract from #842 onward, so
// this opt-out needed no changes anywhere else.

#define LIBBACKTRACE_DIR "vendor/libbacktrace"

// Named variant (#850): lets a graph that needs two libbacktrace archives at
// once avoid the duplicate-target-name check (#842 Step 1).
// make_libbacktrace(ctx) below is the common case: same body, fixed name.
static BuildTarget *make_libbacktrace_named(Builder *ctx, const char *name) {
    const char *has_bt = GetEnv(ctx, "CCCC_HAS_BACKTRACE");
    if (has_bt && strcmp(has_bt, "0") == 0)
        return NULL;
    if (!FileExists(ctx, LIBBACKTRACE_DIR "/backtrace.c")) {
        static bool warned;
        if (!warned)
            fprintf(stderr,
                    "note: building without libbacktrace; run `git submodule "
                    "update --init` to enable it\n");
        warned = true;
        return NULL;
    }
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
    AddInclude(bt, LIBBACKTRACE_DIR);
    // Common sources (platform-independent)
    AddSource(bt, LIBBACKTRACE_DIR "/backtrace.c");
    AddSource(bt, LIBBACKTRACE_DIR "/atomic.c");
    AddSource(bt, LIBBACKTRACE_DIR "/dwarf.c");
    AddSource(bt, LIBBACKTRACE_DIR "/fileline.c");
    AddSource(bt, LIBBACKTRACE_DIR "/mmap.c");
    AddSource(bt, LIBBACKTRACE_DIR "/mmapio.c");
    AddSource(bt, LIBBACKTRACE_DIR "/posix.c");
    AddSource(bt, LIBBACKTRACE_DIR "/print.c");
    AddSource(bt, LIBBACKTRACE_DIR "/simple.c");
    AddSource(bt, LIBBACKTRACE_DIR "/sort.c");
    AddSource(bt, LIBBACKTRACE_DIR "/state.c");
    // Platform-specific debug format reader
    if (strcmp(BuildHost(ctx), "darwin") == 0)
        AddSource(bt, LIBBACKTRACE_DIR "/macho.c");
    else
        AddSource(bt, LIBBACKTRACE_DIR "/elf.c");
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

// src/std_table.c and src/shim_table.c embed every bundled header and -c=native
// shim chunk with `.incbin`, which the host compiler's depfile never reports,
// so they get a library of their own whose inputs are those files: editing one
// recompiles only these two files.
static bool building_embed_lib;
static BuildTarget *embed_lib(Builder *ctx, BuildTarget *bt);

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
        // src/watch.c's dmon backend is FSEvents.
        AddLdFlag(t, "-framework");
        AddLdFlag(t, "CoreServices");
    }
    probe_libffi(ctx, t);
    maybe_add_curl(ctx, t);
    maybe_add_decimal(ctx, t);
    maybe_add_ndbm(ctx, t);
    if (bt) {
        AddDefine(t, "CCCC_HAS_BACKTRACE", "1");
        AddInclude(t, "src/backtrace");
        AddInclude(t, LIBBACKTRACE_DIR);
        LinkWith(t, bt);
    }
    if (!building_embed_lib)
        LinkWith(t, embed_lib(ctx, bt));
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

static BuildTarget *embed_lib(Builder *ctx, BuildTarget *bt) {
    static BuildTarget *lib;
    if (lib)
        return lib;
    lib = StaticLib(ctx, "embedded");
    AddSource(lib, "src/std_table.c");
    AddSource(lib, "src/shim_table.c");
    AddCFlag(lib, "-fPIC");
    building_embed_lib = true;
    add_cccc_flags(ctx, lib, bt);
    building_embed_lib   = false;
    const char **headers = GlobFiles(ctx, "include/*.h");
    for (int i = 0; headers[i]; i++)
        AddInput(lib, headers[i]);
    headers = GlobFiles(ctx, "include/*/*.h");
    for (int i = 0; headers[i]; i++)
        AddInput(lib, headers[i]);
    const char **shims = GlobFiles(ctx, "src/shims/*/*.c");
    for (int i = 0; shims[i]; i++)
        AddInput(lib, shims[i]);
    AddInput(lib, "src/stdlib/wide_bitint/runtime.c");
    AddInput(lib, "src/stdlib/c23_math/runtime.c");
    AddInput(lib, "src/stdlib/c23_mem/runtime.c");
    AddInput(lib, "src/stdlib/c23_strtol/runtime.c");
    return lib;
}

// ---- Source glob shared by all cccc executable targets -------------------

static void add_cccc_sources(Builder *ctx, BuildTarget *t) {
    AddSourcesGlob(t, "src/*.c");
    AddSourcesGlob(t, "src/stdlib/*.c");
    ExcludeSource(t, "src/ops.c");
    ExcludeSource(t, "src/std_table.c"); // linked via embed_lib()
    ExcludeSource(t, "src/shim_table.c");
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

[[cccc::build_target]]
BuildTarget *release(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_cccc_exe_named_opt(ctx, bt, "cccc-release", "-O2", true);
}

// ---- suite tests -----------------------------------------------------------
// One RunCustom per tests/suites/test_*.c, so `--build-jobs=N` runs suites in
// parallel and `--build-keep-going` reports every failing suite. A suite's
// flags come from a `// CCCC_FLAGS:` line in its leading comment block
// (default `--testing`); `// CCCC_NATIVE_SKIP: <reason>` leaves it out of the
// -c=native pass.

static int suite_header_value(const char *path, const char *key, char *out,
                              size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char   line[512];
    size_t klen  = strlen(key);
    int    found = 0;
    for (int i = 0; i < 40 && !found && fgets(line, sizeof(line), f); i++) {
        const char *p = line;
        if (strncmp(p, "//", 2) != 0)
            continue;
        p += 2;
        while (*p == ' ')
            p++;
        if (strncmp(p, key, klen) != 0 || p[klen] != ':')
            continue;
        p += klen + 1;
        while (*p == ' ')
            p++;
        snprintf(out, cap, "%s", p);
        out[strcspn(out, "\n")] = '\0';
        found                   = 1;
    }
    fclose(f);
    return found;
}

// `--testing` becomes `--testing=native` when native is set.
static void suite_command(char *cmd, size_t cap, const char *cccc_path,
                          const char *flags, const char *file, int native) {
    int  n = snprintf(cmd, cap, "env CCCC_NO_CONFIG=1 \"%s\" -I./include",
                      cccc_path);
    char copy[512];
    snprintf(copy, sizeof(copy), "%s", flags);
    for (char *tok = strtok(copy, " \t"); tok; tok = strtok(NULL, " \t")) {
        const char *t  = (native && strcmp(tok, "--testing") == 0)
                             ? "--testing=native"
                             : tok;
        n             += snprintf(cmd + n, cap - n, " %s", t);
    }
    snprintf(cmd + n, cap - n, " %s", file);
}

static BuildTarget *suite_tests(Builder *ctx, BuildTarget *cccc,
                                const char *prefix, int native) {
    char agg_name[64];
    snprintf(agg_name, sizeof(agg_name), "%s", prefix);
    BuildTarget *agg   = RunCustom(ctx, agg_name, "true");
    const char **files = GlobFiles(ctx, "tests/suites/test_*.c");
    for (int i = 0; files[i]; i++) {
        const char *base = strrchr(files[i], '/');
        base             = base ? base + 1 : files[i];
        char reason[256], flags[256];
        if (native && suite_header_value(files[i], "CCCC_NATIVE_SKIP", reason,
                                         sizeof(reason)))
            continue;
        if (!suite_header_value(files[i], "CCCC_FLAGS", flags, sizeof(flags)))
            snprintf(flags, sizeof(flags), "--testing");
        char cmd[1024], name[160];
        suite_command(cmd, sizeof(cmd), TargetOutput(cccc), flags, files[i],
                      native);
        snprintf(name, sizeof(name), "%s-%s", prefix, base);
        BuildTarget *step = RunCustom(ctx, name, cmd);
        DependsOn(step, cccc);
        DependsOn(agg, step);
    }
    return agg;
}

// Both passes.
static BuildTarget *make_test_step(Builder *ctx, BuildTarget *cccc,
                                   const char *prefix) {
    char name[64];
    snprintf(name, sizeof(name), "%s-vm", prefix);
    BuildTarget *vm = suite_tests(ctx, cccc, name, 0);
    snprintf(name, sizeof(name), "%s-native", prefix);
    BuildTarget *nat = suite_tests(ctx, cccc, name, 1);
    BuildTarget *all = RunCustom(ctx, prefix, "true");
    DependsOn(all, vm);
    DependsOn(all, nat);
    return all;
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
    char step_name[32];
    snprintf(step_name, sizeof(step_name), "opt-test-%s", level);
    return make_test_step(ctx, cccc, step_name);
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
// bypassing the guest-.c suites, for tests that need
// multiple VirtualMachines or threads in one host process.
//
// Simplification vs the Makefile recipe: the binaries are run as a single
// `&&`-chained RunCustom command (fails fast on the first failure) rather
// than "run every binary, then fail if any of them failed" -- the vendored
// RunCustom shell has no variables or exit-code capture to accumulate a
// fail flag across commands the way the Makefile's `for` loop does.

static BuildTarget *make_host_tests(Builder *ctx, BuildTarget *bt,
                                    BuildTarget *cccc) {
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
        strncat(cmd, "env CCCC_NO_CONFIG=1 ", sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, TargetOutput(t), sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, " </dev/null", sizeof(cmd) - strlen(cmd) - 1);
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
    // test_watch_cli runs build/cccc.
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *host_tests(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_host_tests(ctx, bt, make_cccc_exe_named(ctx, bt, "cccc"));
}

// ---- test / test-vm / test-native --

// The suites in both passes, plus the host tests.
[[cccc::build_target]]
BuildTarget *test(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    BuildTarget *all  = make_test_step(ctx, cccc, "test");
    DependsOn(all, make_host_tests(ctx, bt, cccc));
    return all;
}

// The suites against the -O2 release binary, and against the MSan build
// (Linux + clang only).
[[cccc::build_target]]
BuildTarget *test_release(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_test_step(
        ctx, make_cccc_exe_named_opt(ctx, bt, "cccc-release", "-O2", true),
        "test-release");
}

[[cccc::build_target]]
BuildTarget *test_msan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_test_step(
        ctx, make_sanitizer_variant(ctx, bt, "cccc-msan", "-fsanitize=memory"),
        "test-msan");
}

[[cccc::build_target]]
BuildTarget *test_ubsan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_test_step(
        ctx,
        make_sanitizer_variant(ctx, bt, "cccc-ubsan", "-fsanitize=undefined"),
        "test-ubsan");
}

[[cccc::build_target]]
BuildTarget *test_asan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_test_step(
        ctx,
        make_sanitizer_variant(ctx, bt, "cccc-asan",
                               "-fsanitize=address,undefined"),
        "test-asan");
}

[[cccc::build_target]]
BuildTarget *test_tsan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_test_step(
        ctx, make_sanitizer_variant(ctx, bt, "cccc-tsan", "-fsanitize=thread"),
        "test-tsan");
}

[[cccc::build_target]]
BuildTarget *test_vm(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    return suite_tests(ctx, cccc, "test-vm", 0);
}

[[cccc::build_target]]
BuildTarget *test_native(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    return suite_tests(ctx, cccc, "test-native", 1);
}

// ---- fuzz_replay -----------------------------------------------------------
// Replays tests/fuzz/corpus/*.c through the built cccc (compile-only).

[[cccc::build_target]]
BuildTarget *fuzz_replay(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char         cmd[512];
    snprintf(cmd, sizeof(cmd), "python3 tools/fuzz_replay.py --binary %s",
             TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "fuzz-replay", cmd);
    DependsOn(step, cccc);
    return step;
}

// ---- test_smoke ------------------------------------------------------------
// Multi-step checks of the -m / -c=generated / -c=native output that a single
// testcheck run cannot express (host compiler argv, artifacts written or not,
// linking generated output): one tests/smoke/*.sh script per step, each given
// the cccc binary.

static BuildTarget *make_smoke(Builder *ctx, BuildTarget *cccc) {
    BuildTarget *agg   = RunCustom(ctx, "smoke", "true");
    const char **files = GlobFiles(ctx, "tests/smoke/*.sh");
    for (int i = 0; files[i]; i++) {
        if (strstr(files[i], "/lib.sh"))
            continue;
        const char *base = strrchr(files[i], '/');
        base             = base ? base + 1 : files[i];
        char cmd[512], name[160];
        snprintf(cmd, sizeof(cmd), "sh \"%s\" \"%s\"", files[i],
                 TargetOutput(cccc));
        snprintf(name, sizeof(name), "smoke-%s", base);
        BuildTarget *step = RunCustom(ctx, name, cmd);
        DependsOn(step, cccc);
        DependsOn(agg, step);
    }
    return agg;
}

[[cccc::build_target]]
BuildTarget *test_smoke(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_smoke(ctx, make_cccc_exe_named(ctx, bt, "cccc"));
}

// ---- area tests ------------------------------------------------------------
// The loose tests (tests/test_*.c, tests/macros/test_*.c) are secondary to the
// suites: grouped by filename prefix, and run when work touches that area.
// Each file's leading comment block says how it is checked; see
// tools/testcheck.c. `test_extra` runs every group.

typedef struct {
    const char *target;
    const char *prefixes; // space-separated filename prefixes
} Area;

static const Area AREAS[] = {
    {"test_warnings", "test_warning_"},
    {"test_serialize", "test_serialize_ test_native_"},
    {"test_build_mode", "test_build_"},
    {"test_safety", "test_checked_ test_dangling_ test_heap_"},
    {"test_parallel", "test_kernel_ test_opencl_ test_openmp_"},
    {"test_macros", NULL}, // tests/macros/
    {"test_misc", NULL},   // everything else
};
#define NAREAS ((int)(sizeof(AREAS) / sizeof(AREAS[0])))

static int area_of(const char *path) {
    if (strstr(path, "tests/macros/"))
        return NAREAS - 2;
    const char *base = strrchr(path, '/');
    base             = base ? base + 1 : path;
    for (int i = 0; i < NAREAS - 2; i++) {
        char copy[256];
        snprintf(copy, sizeof(copy), "%s", AREAS[i].prefixes);
        for (char *p = strtok(copy, " "); p; p = strtok(NULL, " "))
            if (strncmp(base, p, strlen(p)) == 0)
                return i;
    }
    return NAREAS - 1;
}

static BuildTarget *make_testcheck(Builder *ctx) {
    BuildTarget *t = Executable(ctx, "testcheck");
    SetOutput(t, "testcheck");
    AddSource(t, "tools/testcheck.c");
    return t;
}

// `native` runs each test through `testcheck --native` (-c=native); `leaks`
// through `testcheck --leaks`.
enum { LOOSE_VM, LOOSE_NATIVE, LOOSE_LEAKS };

static BuildTarget *area_tests_with(Builder *ctx, BuildTarget *cccc,
                                    BuildTarget *check, int area, int mode) {
    char agg_name[64];
    snprintf(agg_name, sizeof(agg_name), "%s%s",
             mode == LOOSE_NATIVE  ? "native-loose-"
             : mode == LOOSE_LEAKS ? "leaks-"
                                   : "",
             AREAS[area].target);
    BuildTarget *agg     = RunCustom(ctx, agg_name, "true");
    const char  *globs[] = {"tests/test_*.c", "tests/macros/test_*.c"};
    for (int g = 0; g < 2; g++) {
        const char **files = GlobFiles(ctx, globs[g]);
        for (int i = 0; files[i]; i++) {
            if (area_of(files[i]) != area)
                continue;
            const char *base = strrchr(files[i], '/');
            base             = base ? base + 1 : files[i];
            char cmd[1024], name[192];
            snprintf(cmd, sizeof(cmd), "\"%s\"%s \"%s\" %s",
                     TargetOutput(check),
                     mode == LOOSE_NATIVE  ? " --native"
                     : mode == LOOSE_LEAKS ? " --leaks"
                                           : "",
                     TargetOutput(cccc), files[i]);
            snprintf(name, sizeof(name), "%s-%s", agg_name, base);
            BuildTarget *step = RunCustom(ctx, name, cmd);
            DependsOn(step, cccc);
            DependsOn(step, check);
            DependsOn(agg, step);
        }
    }
    return agg;
}

static BuildTarget *area_tests(Builder *ctx, int area) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    return area_tests_with(ctx, cccc, make_testcheck(ctx), area, LOOSE_VM);
}

// Every area's loose tests built and run with -c=native.
static BuildTarget *native_loose_tests(Builder *ctx, BuildTarget *cccc,
                                       BuildTarget *check) {
    BuildTarget *all = RunCustom(ctx, "test-native-loose", "true");
    for (int i = 0; i < NAREAS; i++)
        DependsOn(all, area_tests_with(ctx, cccc, check, i, LOOSE_NATIVE));
    return all;
}

[[cccc::build_target]]
BuildTarget *test_warnings(Builder *ctx) {
    return area_tests(ctx, 0);
}

[[cccc::build_target]]
BuildTarget *test_serialize(Builder *ctx) {
    return area_tests(ctx, 1);
}

[[cccc::build_target]]
BuildTarget *test_build_mode(Builder *ctx) {
    return area_tests(ctx, 2);
}

[[cccc::build_target]]
BuildTarget *test_safety(Builder *ctx) {
    return area_tests(ctx, 3);
}

[[cccc::build_target]]
BuildTarget *test_parallel(Builder *ctx) {
    return area_tests(ctx, 4);
}

[[cccc::build_target]]
BuildTarget *test_macros(Builder *ctx) {
    return area_tests(ctx, 5);
}

[[cccc::build_target]]
BuildTarget *test_misc(Builder *ctx) {
    return area_tests(ctx, 6);
}

[[cccc::build_target]]
BuildTarget *test_extra(Builder *ctx) {
    BuildTarget *bt    = make_libbacktrace(ctx);
    BuildTarget *cccc  = make_cccc_exe_named(ctx, bt, "cccc");
    BuildTarget *check = make_testcheck(ctx);
    BuildTarget *all   = RunCustom(ctx, "test-extra", "true");
    for (int i = 0; i < NAREAS; i++)
        DependsOn(all, area_tests_with(ctx, cccc, check, i, LOOSE_VM));
    DependsOn(all, native_loose_tests(ctx, cccc, check));
    DependsOn(all, make_smoke(ctx, cccc));
    return all;
}

// ---- test_leaks ------------------------------------------------------------
// Every suite and loose test under `leaks -atExit` (macOS) or valgrind. Only
// cccc's own process is checked, not the children a --testing run forks; see
// tools/testcheck.c.

static BuildTarget *leak_suite_tests(Builder *ctx, BuildTarget *cccc,
                                     BuildTarget *check) {
    BuildTarget *agg   = RunCustom(ctx, "leaks-suites", "true");
    const char **files = GlobFiles(ctx, "tests/suites/test_*.c");
    for (int i = 0; files[i]; i++) {
        const char *base = strrchr(files[i], '/');
        base             = base ? base + 1 : files[i];
        char cmd[1024], name[160];
        snprintf(cmd, sizeof(cmd), "\"%s\" --leaks --suite \"%s\" %s",
                 TargetOutput(check), TargetOutput(cccc), files[i]);
        snprintf(name, sizeof(name), "leaks-suites-%s", base);
        BuildTarget *step = RunCustom(ctx, name, cmd);
        DependsOn(step, cccc);
        DependsOn(step, check);
        DependsOn(agg, step);
    }
    return agg;
}

[[cccc::build_target]]
BuildTarget *test_leaks(Builder *ctx) {
    const char *tool =
        strcmp(BuildHost(ctx), "darwin") == 0 ? "leaks" : "valgrind";
    if (!HaveTool(ctx, tool)) {
        fprintf(stderr, "build: test_leaks needs %s\n", tool);
        return RunCustom(ctx, "test-leaks", "true");
    }
    BuildTarget *bt    = make_libbacktrace(ctx);
    BuildTarget *cccc  = make_cccc_exe_named(ctx, bt, "cccc");
    BuildTarget *check = make_testcheck(ctx);
    BuildTarget *all   = RunCustom(ctx, "test-leaks", "true");
    DependsOn(all, leak_suite_tests(ctx, cccc, check));
    for (int i = 0; i < NAREAS; i++)
        DependsOn(all, area_tests_with(ctx, cccc, check, i, LOOSE_LEAKS));
    return all;
}

[[cccc::build_target]]
BuildTarget *test_native_loose(Builder *ctx) {
    BuildTarget *bt   = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    return native_loose_tests(ctx, cccc, make_testcheck(ctx));
}

// Doxygen HTML API docs for the public headers.
// Needs no cccc binary, but doxygen is an external dependency, so it's guarded with
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
    BuildTarget *cccc = release(ctx);
    char         cmd[512];
    snprintf(cmd, sizeof(cmd), "python3 tools/bench.py --cccc %s %s",
             TargetOutput(cccc), pyflags);
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

// Peak memory per benchmark plus compiling cccc's own sources (cccc and gcc).
[[cccc::build_target]]
BuildTarget *bench_memory(Builder *ctx) {
    return make_bench_compare(ctx, "bench-memory",
                              "--runs 1 --warmup 0 --selfhost");
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
// into the Makefile's staged -build/-smoke/-test workflow, plus the MSan
// in-container test target. The smoke recipe needs $(...) substitution and
// $? capture, which the vendored build shell doesn't support, so the
// container recipes are delegated to real shell scripts
// (tools/linux_container_smoke.sh, tools/linux_amd64_test.sh,
// tools/linux_amd64_msan_test.sh) rather than inlined into RunCustom strings.

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
    BuildTarget *step = RunCustom(
        ctx, "linux-aarch64-test",
        "colima -p cccc-linux-arm64 nerdctl -- run --rm --platform linux/arm64 "
        "cccc-linux-arm64 timeout 600 ./cccc --build build.c "
        "--build-target=test --build-jobs=8 --build-keep-going");
    DependsOn(step, linux_aarch64_smoke(ctx));
    return step;
}

// MSan build + full in-container test run (Makefile:523-526). Expected to
// report failures: an uninstrumented libc/libffi MSan blind spot
// (documented in docs/test-infrastructure.md, #844) accounts for ~262/700 of them --
// not a regression on its own, compare against that documented baseline.
[[cccc::build_target]]
BuildTarget *linux_amd64_msan_test(Builder *ctx) {
    BuildTarget *step = RunCustom(ctx, "linux-amd64-msan-test",
                                  "sh tools/linux_amd64_msan_test.sh "
                                  "cccc-linux-amd64 cccc-linux-amd64 8");
    DependsOn(step, linux_amd64_build(ctx));
    return step;
}

// ---- ouroboros: cccc built by cccc ---------------------------------------
// Whole-program `cccc -c=native` build of cccc's own sources (see
// docs/selfhosting.md). Run from the repository root: src/std_table.c's
// `.incbin` paths are cwd-relative.

// Adds the -I/-L entries `pkg-config <flag> libffi` prints. CcccExecutable
// takes no PkgConfig() or AddCFlag(), so the flags are split by hand.
static void ouroboros_pkg_flags(Builder *ctx, BuildTarget *t, const char *cmd,
                                bool include) {
    const char *out = CaptureCommand(ctx, cmd);
    if (!out)
        return;
    char *copy = strdup(out);
    for (char *tok = strtok(copy, " \t\n\r"); tok;
         tok       = strtok(NULL, " \t\n\r")) {
        if (include && strncmp(tok, "-I", 2) == 0)
            AddInclude(t, tok + 2);
        else if (!include && strncmp(tok, "-L", 2) == 0)
            AddLibPath(t, tok + 2);
    }
    free(copy);
}

[[cccc::build_target]]
BuildTarget *ouroboros(Builder *ctx) {
    BuildTarget *t = CcccExecutable(ctx, "cccc-ouroboros");
    SetOutput(t, "cccc-ouroboros");
    AddSourcesGlob(t, "src/*.c");
    AddSourcesGlob(t, "src/stdlib/*.c");
    ExcludeSource(t, "src/ops.c");
    AddInclude(t, "include");
    AddInclude(t, "src");
    AddLib(t, "pthread");
    AddLib(t, "ffi");
    if (strcmp(BuildHost(ctx), "darwin") == 0) {
        // CoreServices' SDK headers cannot share a unit with cccc's bundled
        // <signal.h> and <sys/*.h>.
        UseSystemHeaders(t);
        const char *sdk = CaptureCommand(ctx, "xcrun --show-sdk-path");
        char        path[1024];
        snprintf(path, sizeof(path), "%s/usr/include", sdk);
        AddSystemInclude(t, path);
        snprintf(path, sizeof(path), "%s/usr/include/ffi", sdk);
        AddInclude(t, path);
        snprintf(path, sizeof(path), "%s/System/Library/Frameworks", sdk);
        AddFrameworkPath(t, path);
        AddFramework(t, "CoreServices");
        AddLib(t, "iconv");
        AddLibPath(t, "/opt/homebrew/opt/libffi/lib");
        AddLibPath(t, "/usr/local/opt/libffi/lib");
    } else if (HaveTool(ctx, "pkg-config")) {
        ouroboros_pkg_flags(ctx, t, "pkg-config --cflags-only-I libffi", true);
        ouroboros_pkg_flags(ctx, t, "pkg-config --libs-only-L libffi", false);
    }
    if (strcmp(BuildHost(ctx), "linux") == 0) {
        // glibc's real headers, as on macOS: cccc's bundled ones lack POSIX
        // functions its own sources call (strnlen, stpcpy, ...). The multiarch
        // directory holds libffi's ffi.h and asm/*.h.
        UseSystemHeaders(t);
        const char **ffi = GlobFiles(ctx, "/usr/include/*/ffi.h");
        if (ffi[0]) {
            char dir[256];
            snprintf(dir, sizeof(dir), "%s", ffi[0]);
            *strrchr(dir, '/') = '\0';
            AddSystemInclude(t, dir);
        }
        AddSystemInclude(t, "/usr/include");
    }
    char cmd[1100];
    snprintf(cmd, sizeof(cmd), "%s --version", TargetOutput(t));
    BuildTarget *smoke = RunCustom(ctx, "ouroboros-smoke", cmd);
    DependsOn(smoke, t);
    return smoke;
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
// Builds: libbacktrace (vendored) and the final build/cccc executable. All
// other targets are available via --build-target=NAME.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    make_cccc_exe_named(ctx, bt, "cccc");
    return BuildAll(ctx);
}
