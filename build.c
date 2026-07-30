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
//   cccc_asan     AddressSanitizer + UBSan build  → cccc-asan
//   cccc_ubsan    UndefinedBehaviourSanitizer      → cccc-ubsan
//   cccc_tsan     ThreadSanitizer                  → cccc-tsan
//   cccc_msan     MemorySanitizer (Linux+clang)    → cccc-msan
//   fuzz_harness  libFuzzer + ASan harness         → fuzz_harness
//   stdlib_gen    two-pass regenerate src/std.c only (no final rebuild)
//   bench         hyperfine benchmark
//
// Not covered here — use tools/Makefile.backup for:
//   AFL++ fuzzing     (make afl, make afl-asan)
//   Profiling builds  (make profile-cpu, make profile-mem)
//   macOS x86_64 cross-compile (make macos-x86_64-*)
//   Linux x86_64 via Colima    (make linux-x86_64-*)
//   make clean, make dsym

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

// ---- Vendored libbacktrace (src/backtrace/) -------------------------------
// Compiled with distinct flags: no -std=c23 (sources are C99/C11), separate
// warning suppressions, and platform-specific format reader.

static BuildTarget *make_libbacktrace(Builder *ctx) {
    BuildTarget *bt = StaticLib(ctx, "backtrace");
    AddCFlag(bt, "-O2");
    AddCFlag(bt, "-g");
    AddCFlag(bt, "-std=c11");
    AddCFlag(bt, "-Wno-unused-parameter");
    AddCFlag(bt, "-Wno-unused-variable");
    AddCFlag(bt, "-Wno-missing-field-initializers");
    AddCFlag(bt, "-Wno-shift-count-overflow");
    AddCFlag(bt, "-Wno-implicit-function-declaration");
    AddCFlag(bt, "-Wno-deprecated-declarations");
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

// ---- Common compile + link flags for all cccc-family targets --------------

static void add_cccc_flags(Builder *ctx, BuildTarget *t, BuildTarget *bt) {
    AddCFlag(t, "-Wall");
    AddCFlag(t, "-O0");
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
    if (bt) {
        AddDefine(t, "CCCC_HAS_BACKTRACE", "1");
        AddInclude(t, "src/backtrace");
        LinkWith(t, bt);
    }
}

// ---- Source glob shared by all cccc executable targets -------------------

static void add_cccc_sources(Builder *ctx, BuildTarget *t) {
    AddSourcesGlob(t, "src/*.c");
    AddSourcesGlob(t, "src/stdlib/*.c");
    ExcludeSource(t, "src/ops.c");
    // src/std.c (generated) and src/std_seed.c (committed stage0 fallback,
    // #842 Step 3) both define get_std_header/get_stdlib_reg_fn_name/
    // get_std_header_name -- never compile both into the same target.
    // std.c wins whenever it exists on disk; same self-correcting rule the
    // Makefile's SRCS uses.
    if (FileExists(ctx, "src/std.c"))
        ExcludeSource(t, "src/std_seed.c");
    else
        ExcludeSource(t, "src/std.c"); // no-op when absent; harmless
}

// ---- Reusable cccc executable factory ------------------------------------

static BuildTarget *make_cccc_exe_named(Builder *ctx, BuildTarget *bt, const char *name) {
    BuildTarget *t = Executable(ctx, name);
    SetOutput(t, name);
    add_cccc_sources(ctx, t);
    add_cccc_flags(ctx, t, bt);
    return t;
}

// ---- [[cccc::build_target]] factories (invoked via --build-target=NAME) --

// Shared by the four cccc_* sanitizer factories below and by sanitizers()
// (which must build all four in ONE graph, so it passes in a single shared
// `bt` -- calling make_libbacktrace() once per factory, as each did
// standalone, would declare four "backtrace" targets in the same graph and
// trip the duplicate-name check added in #842 Step 1).
static BuildTarget *make_sanitizer_variant(Builder *ctx, BuildTarget *bt,
                                            const char *name, const char *sanitize_flag) {
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
    return make_sanitizer_variant(ctx, bt, "cccc-asan", "-fsanitize=address,undefined");
}

[[cccc::build_target]]
BuildTarget *cccc_ubsan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return make_sanitizer_variant(ctx, bt, "cccc-ubsan", "-fsanitize=undefined");
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
    BuildTarget *t = Executable(ctx, "fuzz_harness");
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
    BuildTarget *t = DynamicLib(ctx, "cccc"); // -> lib/libcccc.{dylib,so}
    add_cccc_sources(ctx, t);
    ExcludeSource(t, "src/main.c"); // no main() in a shared library
    add_cccc_flags(ctx, t, bt);
    AddCFlag(t, "-fPIC");
    return t;
}

// ---- Sanitizer aggregate (Makefile:217-227) --------------------------------
// No meta-target concept exists in the builder API; a no-op RunCustom that
// DependsOn's the four (three on macOS: msan needs Linux+clang) sanitizer
// targets is the cheapest correct shape.

[[cccc::build_target]]
BuildTarget *sanitizers(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *asan = make_sanitizer_variant(ctx, bt, "cccc-asan", "-fsanitize=address,undefined");
    BuildTarget *ubsan = make_sanitizer_variant(ctx, bt, "cccc-ubsan", "-fsanitize=undefined");
    BuildTarget *tsan = make_sanitizer_variant(ctx, bt, "cccc-tsan", "-fsanitize=thread");
    BuildTarget *step = RunCustom(ctx, "sanitizers", "true");
    DependsOn(step, asan);
    DependsOn(step, ubsan);
    DependsOn(step, tsan);
    if (strcmp(BuildHost(ctx), "linux") == 0)
        DependsOn(step, make_sanitizer_variant(ctx, bt, "cccc-msan", "-fsanitize=memory"));
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
    DeleteDir(ctx, "fuzz/corpus");
    DeleteDir(ctx, "fuzz/out");
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
    BuildTarget *bt = make_libbacktrace(ctx);
    const char **files = GlobFiles(ctx, "tests/host/test_*.c");
    BuildTarget *tests[256];
    int n = 0;
    char cmd[8192];
    cmd[0] = '\0';
    for (int i = 0; files[i] && n < 256; i++) {
        const char *path = files[i];
        const char *slash = strrchr(path, '/');
        const char *base = slash ? slash + 1 : path;
        char name[128];
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
        fprintf(stderr, "build: host_tests found no tests/host/test_*.c files\n");
        return RunCustom(ctx, "host-tests", "true");
    }
    BuildTarget *step = RunCustom(ctx, "host-tests", cmd);
    for (int i = 0; i < n; i++)
        DependsOn(step, tests[i]);
    return step;
}

// ---- test / test-suites / test-legacy / sqlite-smoke / audit-ffi (Makefile:368-389) --

[[cccc::build_target]]
BuildTarget *test(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "python3 tools/run_tests.py --binary %s", TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "test", cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *test_suites(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "python3 tools/tests.py --suites --binary %s", TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "test-suites", cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *test_legacy(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "python3 tools/tests.py --legacy --binary %s", TargetOutput(cccc));
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
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "cp %s ./cccc.sqlite-smoke-tmp && mv ./cccc.sqlite-smoke-tmp ./cccc "
        "&& python3 tools/sqlite_smoke.py",
        TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "sqlite-smoke", cmd);
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *audit_ffi(Builder *ctx) {
    // Pure source scan (src/stdlib/*.c FFI registrations vs include/**/*.h
    // declarations); no build required (Makefile:384-388).
    return RunCustom(ctx, "audit-ffi", "python3 tools/audit_ffi.py");
}

// ---- bench-compare{,-quick,-json} (Makefile:551-564) -----------------------

static BuildTarget *make_bench_compare(Builder *ctx, const char *name, const char *pyflags) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    char cmd[512];
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
    return make_bench_compare(ctx, "bench-compare-quick", "--runs 2 --warmup 1");
}

[[cccc::build_target]]
BuildTarget *bench_compare_json(Builder *ctx) {
    return make_bench_compare(ctx, "bench-compare-json", "--format json --runs 3 --warmup 1");
}

// ---- profile-cpu / profile-mem (Makefile:566-599) --------------------------

[[cccc::build_target]]
BuildTarget *profile_cpu(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t = Executable(ctx, "cccc-prof");
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
    //
    // `|| true` matches Makefile:592's own tolerance for this step failing:
    // PROFILE_TEST (tests/test_comprehensive.c) does not currently exist in
    // this repo (filed as a follow-up), so both the Makefile's recipe and
    // this one already run against a missing file -- `|| true` keeps the
    // (pre-existing, tracked separately) gap from also failing the build.
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "mkdir -p profile && env CPUPROFILE=profile/cpu.prof %s -I./include tests/test_comprehensive.c || true",
        TargetOutput(t));
    BuildTarget *step = RunCustom(ctx, "profile-cpu", cmd);
    DependsOn(step, t);
    return step;
}

[[cccc::build_target]]
BuildTarget *profile_mem(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    // See profile_cpu()'s comment: tests/test_comprehensive.c does not
    // currently exist, so `|| true` matches the Makefile's own tolerance
    // for this (pre-existing, tracked separately) gap.
    char cmd[512];
    if (strcmp(BuildHost(ctx), "darwin") == 0)
        snprintf(cmd, sizeof(cmd),
            "mkdir -p profile && leaks -atExit -- %s -I./include tests/test_comprehensive.c "
            "> profile/mem-leaks.txt 2>&1 || true",
            TargetOutput(cccc));
    else
        snprintf(cmd, sizeof(cmd),
            "mkdir -p profile && valgrind --tool=massif --massif-out-file=profile/mem.massif "
            "%s -I./include tests/test_comprehensive.c || true",
            TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "profile-mem", cmd);
    DependsOn(step, cccc);
    return step;
}

// ---- dsym (Makefile:608-617, macOS-only) -----------------------------------

[[cccc::build_target]]
BuildTarget *dsym(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    BuildTarget *step;
    if (strcmp(BuildHost(ctx), "darwin") == 0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "dsymutil %s", TargetOutput(cccc));
        step = RunCustom(ctx, "dsym", cmd);
    } else {
        step = RunCustom(ctx, "dsym",
            "echo 'dsym: no-op on this platform (DWARF is already in the ELF)'");
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
    if (cc) return cc;
    return FindTool(ctx, "afl-clang");
}

[[cccc::build_target]]
BuildTarget *afl(Builder *ctx) {
    const char *afl_cc = find_afl_cc(ctx);
    if (!afl_cc) {
        fprintf(stderr, "build: afl requires afl-clang-fast or afl-clang (AFL++ not found)\n");
        return RunCustom(ctx, "afl", "false");
    }
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t = Executable(ctx, "cccc-afl");
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
        fprintf(stderr, "build: afl-asan requires afl-clang-fast or afl-clang (AFL++ not found)\n");
        return RunCustom(ctx, "afl-asan", "false");
    }
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t = Executable(ctx, "cccc-afl-asan");
    SetOutput(t, "cccc-afl-asan");
    add_cccc_sources(ctx, t);
    add_cccc_flags(ctx, t, bt);
    SetToolchain(t, afl_cc);
    SetTargetEnv(t, "AFL_USE_ASAN", "1");
    return t;
}

// ---- macOS x86_64 cross-compile (Makefile:391-456) -------------------------
// Cross-build only (SetTargetTriple + SDK include paths -- clang's
// `--target=x86_64-apple-macos` cross-compiles without needing a different
// compiler binary or SetToolchain's `-arch x86_64`, which does not work
// there: SetToolchain treats its argument as a single executable path, not
// a compiler-plus-flags command line, so "/usr/bin/clang -arch x86_64"
// fails execvp with "No such file or directory"). The Makefile's Rosetta
// smoke/test targets additionally shell out to `/usr/bin/arch -x86_64` and
// the system python3, which is orchestration rather than target declaration
// -- left to tools/Makefile.backup for now. Unlike the Makefile (which
// hardcodes /usr/local/opt/readline and thereby defeats its own readline
// auto-probe), no readline override is added here.

[[cccc::build_target]]
BuildTarget *macos_x86_64(Builder *ctx) {
    if (strcmp(BuildHost(ctx), "darwin") != 0) {
        fprintf(stderr, "build: macos_x86_64 requires macOS\n");
        return RunCustom(ctx, "macos-x86_64", "false");
    }
    const char *sdk = CaptureCommand(ctx, "xcrun --sdk macosx --show-sdk-path");
    if (!sdk || !*sdk) {
        fprintf(stderr, "build: macOS SDK not found. Install the Xcode Command Line Tools.\n");
        return RunCustom(ctx, "macos-x86_64", "false");
    }
    BuildTarget *bt = make_libbacktrace(ctx);
    SetTargetTriple(bt, "x86_64-apple-macos");
    BuildTarget *t = Executable(ctx, "cccc-macos-x86_64");
    SetOutput(t, "cccc-macos-x86_64");
    add_cccc_sources(ctx, t);
    add_cccc_flags(ctx, t, bt);
    SetTargetTriple(t, "x86_64-apple-macos");
    char ffi_inc[512];
    snprintf(ffi_inc, sizeof(ffi_inc), "%s/usr/include/ffi", sdk);
    AddInclude(t, ffi_inc);
    AddLib(t, "ffi");
    return t;
}

// ---- Linux via Colima (Makefile:458-529) -----------------------------------
// Orchestration around `colima -p <profile> nerdctl -- run ...`, run from
// the host (these do not build anything themselves -- the container image
// already has its own toolchain, per the Dockerfile). Simplified relative
// to the Makefile: single-shot (no 5-way source-pattern sharding under a
// per-shard timeout); use `make linux-x86_64-test` / `make
// linux-aarch64-test` for the full sharded/timeout-guarded run.

[[cccc::build_target]]
BuildTarget *linux_amd64_test(Builder *ctx) {
    return RunCustom(ctx, "linux-amd64-test",
        "colima -p cccc-linux-amd64 nerdctl -- run --rm --platform linux/amd64 "
        "cccc-linux-amd64 python3 tools/run_tests.py -j 8");
}

[[cccc::build_target]]
BuildTarget *linux_aarch64_test(Builder *ctx) {
    return RunCustom(ctx, "linux-aarch64-test",
        "colima -p cccc-linux-arm64 nerdctl -- run --rm "
        "cccc-linux-arm64 python3 tools/run_tests.py -j 8");
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
static BuildTarget *stdlib_regen_step(Builder *ctx, BuildTarget *bt) {
    BuildTarget *pass1 = make_cccc_exe_named(ctx, bt, "cccc-pass1");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "sh tools/regen_stdlib.sh %s", TargetOutput(pass1));
    BuildTarget *gen = RunCustom(ctx, "stdlib-regen", cmd);
    DeclareOutput(gen, "src/std.c");
    DependsOn(gen, pass1);
    return gen;
}

[[cccc::build_target]]
BuildTarget *stdlib_gen(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    return stdlib_regen_step(ctx, bt);
}

[[cccc::build_target]]
BuildTarget *bench(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe_named(ctx, bt, "cccc");
    if (!HaveTool(ctx, "hyperfine")) {
        fprintf(stderr, "build: bench requires hyperfine (not found in PATH) — "
                        "install it (e.g. brew install hyperfine / apt install hyperfine)\n");
        return RunCustom(ctx, "bench", "false");
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "mkdir -p profile && hyperfine --warmup 3 --ignore-failure "
        "--export-json profile/bench.json '%s -I./include tests/test_comprehensive.c'",
        TargetOutput(cccc));
    BuildTarget *step = RunCustom(ctx, "bench", cmd);
    DependsOn(step, cccc);
    return step;
}

// ---- Default build entry -------------------------------------------------
// Builds: libbacktrace (vendored), the two-pass stdlib regen, and the final
// cccc executable. All other targets are available via --build-target=NAME.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *gen = stdlib_regen_step(ctx, bt);
    BuildTarget *final = make_cccc_exe_named(ctx, bt, "cccc");
    DependsOn(final, gen);
    return BuildAll(ctx);
}
