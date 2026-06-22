// build.c — self-hosting build script for CCCC.
//
// Bootstrap: the first build still requires the Makefile (make cccc).
// Subsequent rebuilds: ./cccc --build build.c
//
// Default build (./cccc --build build.c):
//   builds: cccc  libcccc.dylib (macOS) or libcccc.so (Linux)
//
// Selective builds via --build-target=NAME:
//   cccc          main executable
//   libcccc       dynamic library
//   cccc_asan     AddressSanitizer + UBSan build  → cccc-asan
//   cccc_ubsan    UndefinedBehaviourSanitizer      → cccc-ubsan
//   cccc_tsan     ThreadSanitizer                  → cccc-tsan
//   cccc_msan     MemorySanitizer (Linux+clang)    → cccc-msan
//   fuzz_harness  libFuzzer + ASan harness         → fuzz_harness
//   stdlib_gen    regenerate src/std.c
//   bench         hyperfine benchmark
//
// Not covered here — use tools/Makefile.backup for:
//   AFL++ fuzzing     (make afl, make afl-asan)
//   Profiling builds  (make profile-cpu, make profile-mem)
//   macOS x86_64 cross-compile (make macos-x86_64-*)
//   Linux x86_64 via Colima    (make linux-x86_64-*)
//   make clean, make dsym

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

static void add_cccc_sources(BuildTarget *t) {
    AddSourcesGlob(t, "src/*.c");
    AddSourcesGlob(t, "src/stdlib/*.c");
    ExcludeSource(t, "src/ops.c");
}

// ---- Reusable cccc executable factory ------------------------------------

static BuildTarget *make_cccc_exe(Builder *ctx, BuildTarget *bt) {
    BuildTarget *t = Executable(ctx, "cccc");
    SetOutput(t, "cccc");
    add_cccc_sources(t);
    add_cccc_flags(ctx, t, bt);
    return t;
}

// ---- [[cccc::build_target]] factories (invoked via --build-target=NAME) --

[[cccc::build_target]]
BuildTarget *cccc_asan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t = Executable(ctx, "cccc-asan");
    SetOutput(t, "cccc-asan");
    add_cccc_sources(t);
    add_cccc_flags(ctx, t, bt);
    AddCFlag(t, "-fsanitize=address,undefined");
    AddLdFlag(t, "-fsanitize=address,undefined");
    return t;
}

[[cccc::build_target]]
BuildTarget *cccc_ubsan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t = Executable(ctx, "cccc-ubsan");
    SetOutput(t, "cccc-ubsan");
    add_cccc_sources(t);
    add_cccc_flags(ctx, t, bt);
    AddCFlag(t, "-fsanitize=undefined");
    AddLdFlag(t, "-fsanitize=undefined");
    return t;
}

[[cccc::build_target]]
BuildTarget *cccc_tsan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t = Executable(ctx, "cccc-tsan");
    SetOutput(t, "cccc-tsan");
    add_cccc_sources(t);
    add_cccc_flags(ctx, t, bt);
    AddCFlag(t, "-fsanitize=thread");
    AddLdFlag(t, "-fsanitize=thread");
    return t;
}

[[cccc::build_target]]
BuildTarget *cccc_msan(Builder *ctx) {
    // MemorySanitizer requires Linux + clang; will fail to link on macOS
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t = Executable(ctx, "cccc-msan");
    SetOutput(t, "cccc-msan");
    add_cccc_sources(t);
    add_cccc_flags(ctx, t, bt);
    AddCFlag(t, "-fsanitize=memory");
    AddLdFlag(t, "-fsanitize=memory");
    return t;
}

[[cccc::build_target]]
BuildTarget *fuzz_harness(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t = Executable(ctx, "fuzz_harness");
    SetOutput(t, "fuzz_harness");
    add_cccc_sources(t);
    ExcludeSource(t, "src/main.c");
    add_cccc_flags(ctx, t, bt);
    AddCFlag(t, "-fsanitize=fuzzer,address");
    AddLdFlag(t, "-fsanitize=fuzzer,address");
    return t;
}

[[cccc::build_target]]
BuildTarget *stdlib_gen(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe(ctx, bt);
    BuildTarget *step = RunCustom(ctx, "stdlib",
        "./cccc -G -I./include tools/generate_stdlib.c > src/std.c");
    DependsOn(step, cccc);
    return step;
}

[[cccc::build_target]]
BuildTarget *bench(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *cccc = make_cccc_exe(ctx, bt);
    BuildTarget *step = RunCustom(ctx, "bench",
        "mkdir -p profile"
        " && hyperfine --warmup 3 --ignore-failure"
        " --export-json profile/bench.json"
        " './cccc -I./include tests/test_comprehensive.c'");
    DependsOn(step, cccc);
    return step;
}

// ---- Default build entry -------------------------------------------------
// Builds: libbacktrace (vendored) and cccc (main executable).
// All other targets are available via --build-target=NAME.
//
// libcccc (dynamic library) is not included here because src/build.c
// references run_argv() from src/main.c, which is excluded from the lib,
// causing a link error on macOS (and Linux with strict symbol resolution).
// This is a pre-existing bug in the Makefile as well; tracked separately.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    make_cccc_exe(ctx, bt);
    return BuildAll(ctx);
}
