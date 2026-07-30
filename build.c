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

[[cccc::build_target]]
BuildTarget *cccc_asan(Builder *ctx) {
    BuildTarget *bt = make_libbacktrace(ctx);
    BuildTarget *t = Executable(ctx, "cccc-asan");
    SetOutput(t, "cccc-asan");
    add_cccc_sources(ctx, t);
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
    add_cccc_sources(ctx, t);
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
    add_cccc_sources(ctx, t);
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
    add_cccc_sources(ctx, t);
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
    add_cccc_sources(ctx, t);
    ExcludeSource(t, "src/main.c");
    add_cccc_flags(ctx, t, bt);
    AddCFlag(t, "-fsanitize=fuzzer,address");
    AddLdFlag(t, "-fsanitize=fuzzer,address");
    return t;
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
