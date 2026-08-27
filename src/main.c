/*
 CCCC: Comprehensiev C Compensation Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "./cccc.h"
#include "./internal.h"
#include <getopt.h>
#if defined(_WIN32)
#include <io.h>
#define CCCC_ISATTY _isatty
#define CCCC_FILENO _fileno
#else
#include <fnmatch.h>
#include <unistd.h>
#include <sys/wait.h>
#define CCCC_ISATTY isatty
#define CCCC_FILENO fileno
#endif

// Host-side dirname, for the -I forwarded to the native compiler below.
// Doesn't mutate its argument (unlike POSIX dirname()) and doesn't need
// <libgen.h> (which is a CCCC-guest-only polyfill, not a host header here).
static char *host_dirname_dup(const char *path) {
    if (!path || !*path)
        return strdup(".");
    const char *slash = strrchr(path, '/');
    if (!slash)
        return strdup(".");
    if (slash == path)
        return strdup("/");
    size_t len = (size_t)(slash - path);
    char  *dir = malloc(len + 1);
    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

// #1006: run_native_backend()'s -I forwarding used to name only
// dirname(primary_file) -- fine when every replayed #include came from
// input_files[0], but #1006 widened auto-capture (preprocess.c) to replay
// every command-line input file's own #includes, so a non-primary TU's
// quoted `#include "local.h"` needs its own directory forwarded too or the
// host compiler can't resolve it. Iterates vm->compiler.command_line_inputs
// (populated in main() before preprocessing, keyed by the exact strings
// input_files[] held) pushing one -I per distinct directory; dedup is by
// string identity against what's already been pushed, not path
// canonicalization -- consistent with cc_file_is_command_line_input()'s own
// no-canonicalization contract.
typedef struct {
    ArgVec *cc_args;
    char  **seen;
    int     seen_len;
    int     seen_cap;
} NativeIncludeDirCtx;

static int push_input_dir_i(char *key, int keylen, void *val, void *user_data) {
    (void)keylen;
    (void)val;
    NativeIncludeDirCtx *ctx = user_data;
    char                *dir = host_dirname_dup(key);
    for (int i = 0; i < ctx->seen_len; i++) {
        if (strcmp(ctx->seen[i], dir) == 0) {
            free(dir);
            return 0;
        }
    }
    if (ctx->seen_len == ctx->seen_cap) {
        ctx->seen_cap = ctx->seen_cap ? ctx->seen_cap * 2 : 8;
        ctx->seen     = realloc(ctx->seen, sizeof(char *) * ctx->seen_cap);
    }
    ctx->seen[ctx->seen_len++] = dir;
    argv_push(ctx->cc_args, "-I");
    argv_push(ctx->cc_args, dir);
    return 0;
}

// #1065: argv_push() (src/exec.c) stores the pointer it's given, it does
// not copy the string -- so a flag assembled into a stack buffer (as
// run_native_backend() used to do for -std/-D/-U/-l) is either out of
// scope by exec() time, or -- for a buffer declared inside a loop body --
// reused across iterations, silently collapsing "-DA=1 -DB=2" into two
// identical "-DB=2" entries. Heap-allocate instead and track the
// allocation in `owned` so it survives to exec() and gets freed
// afterward, mirroring push_compile_flags()'s own StringArray pattern
// (src/build.c).
static void push_owned_flag(ArgVec *cc_args, StringArray *owned,
                            const char *prefix, const char *value) {
    size_t len  = strlen(prefix) + strlen(value) + 1;
    char  *flag = malloc(len);
    if (!flag)
        error("failed to allocate native cc flag");
    snprintf(flag, len, "%s%s", prefix, value);
    strarray_push(owned, flag);
    argv_push(cc_args, flag);
}

// #1053: CCCC's own internal default standard is gnu23 (src/vm.c), but
// -c=native only ever forwarded -std= when the user passed --std=
// explicitly on the CCCC command line -- a plain `cccc foo.c -c=native`
// relied entirely on the host cc's own default standard, which can be
// older (e.g. Ubuntu's plain `cc` -> gcc defaults to gnu17, see
// man/TESTING.md), silently rejecting any C23 construct the serializer
// legitimately emitted even though the VM run succeeded. Forwarding
// CCCC's resolved default unconditionally isn't safe either -- a host cc
// that doesn't recognize "-std=gnu23" at all would then fail *every*
// -c=native compile outright, trading a narrow failure for a total one.
// Instead, probe the host cc (quietly -- a rejected rung's diagnostic
// isn't the user's business) down a ladder from CCCC's resolved default
// toward older standards, and forward the newest rung it actually
// accepts. "2x" is its own separate rung from "23": gcc 13 accepts
// "-std=gnu2x" but rejects "-std=gnu23" outright. If nothing in the
// ladder is accepted, forward nothing -- exactly today's behaviour, so
// this can never make a native compile that used to succeed fail.
// #1073: when the user passes --std= explicitly, the equivalent-spelling
// rungs (e.g. "23"/"2x", both CCCC_STD_C23) are tried, but the ladder never
// descends to an OLDER standard -- a user who named C23 must never
// silently get C17 semantics on the native half while the VM half stayed
// C23. explicit_std selects between that narrower probe and the full
// best-effort descend-to-older ladder the implicit-default path already
// used (nothing was named there, so falling back is fine). Every spelling
// below is probe-verified, not guessed: Apple clang 17 accepts all of
// them; Ubuntu GCC 13.3.0 rejects "23"/"gnu23" outright but accepts
// "2x"/"gnu2x" and every other listed spelling.
static const char *native_resolve_std_ladder(VirtualMachine *vm, const char *cc,
                                             bool explicit_std) {
    static bool probed = false;
    static bool found  = false;
    static char cached[16];
    if (probed)
        return found ? cached : NULL;
    probed = true;

    // #1187: always probe "gnu<NN>" before "c<NN>", regardless of which
    // prefix the user actually typed. CCCC's own frontend is uniformly
    // permissive -- c_std_gnu (Compiler.c_std_gnu) has no reader left
    // besides this function, and the serializer emits a fixed GNU C11
    // floor no matter what --std= was passed (see man/COVERAGE.md) -- so a
    // strict ISO `c<NN>` spelling forwarded to the host compiler is a
    // promise the rest of CCCC does not keep. A real host GCC's strict
    // `-std=c89` rejects constructs (`//` comments, mixed declarations,
    // VLAs, compound literals, designated initializers) that CCCC's own
    // `--std=c89` only pedantic-warns on -- the identical guest program
    // that compiled and ran fine under the VM would then fail to compile
    // natively for a dialect reason CCCC itself never enforced. Trying
    // "gnu<NN>" first (falling back to "c<NN>" only if the host rejects
    // it) restores "VM passes => native passes" without weakening
    // anything: a host that accepts neither spelling still gets nothing
    // forwarded, same as before.
    const char *prefixes[2];
    int         prefix_n = 0;
    prefixes[prefix_n++] = "gnu";
    if (!vm->compiler.c_std_gnu)
        prefixes[prefix_n++] = "c";
    const char *suffixes[6];
    int         n = 0;
    switch (vm->compiler.c_std) {
        case CCCC_STD_C23:
            suffixes[n++] = "23";
            suffixes[n++] = "2x";
            if (!explicit_std) {
                suffixes[n++] = "17";
                suffixes[n++] = "11";
            }
            break;
        case CCCC_STD_C17:
            suffixes[n++] = "17";
            suffixes[n++] = "18";
            if (!explicit_std)
                suffixes[n++] = "11";
            break;
        case CCCC_STD_C11:
            suffixes[n++] = "11";
            suffixes[n++] = "1x";
            break;
        case CCCC_STD_C99:
            suffixes[n++] = "99";
            suffixes[n++] = "9x";
            break;
        case CCCC_STD_C89:
            suffixes[n++] = "89";
            suffixes[n++] = "90";
            break;
    }

    for (int i = 0; i < n; i++) {
        for (int p = 0; p < prefix_n; p++) {
            char cand[16];
            snprintf(cand, sizeof(cand), "%s%s", prefixes[p], suffixes[i]);
            char probe_flag[24];
            snprintf(probe_flag, sizeof(probe_flag), "-std=%s", cand);
            char *probe_argv[] = {(char *)cc, "-fsyntax-only", probe_flag, "-x",
                                  "c",        "/dev/null",     NULL};
            if (run_argv_quiet(probe_argv) == 0) {
                snprintf(cached, sizeof(cached), "%s", cand);
                found = true;
                return cached;
            }
        }
    }
    return NULL;
}

static int run_native_backend(
    VirtualMachine *vm, Obj *prog, const char *out_file, const char **inc_paths,
    int inc_paths_count, const char **sys_inc_paths, int sys_inc_paths_count,
    const char **lib_paths, int lib_paths_count, const char **libs,
    int libs_count, const char **defines, int defines_count,
    const char **undefs, int undefs_count, const char *std_arg, bool emit_cccc,
    bool emit_test_harness, int opt_level) {
    if (!out_file) {
        fprintf(
            stderr,
            "error: -c=native requires -o <file> (no executable path given)\n");
        return 1;
    }

    if (emit_cccc && !getenv("CCCC_NATIVE_CC")) {
        // --emit-cccc's output carries CCCC-only dialect syntax (@-attrs,
        // [[cccc::...]], etc.) that a plain cc/clang/gcc cannot parse, so
        // the usual cc/clang/gcc PATH search is not offered as a silent
        // default here -- the caller must name a compiler that understands
        // the dialect explicitly via CCCC_NATIVE_CC.
        fprintf(stderr,
                "error: -c=native --emit-cccc requires CCCC_NATIVE_CC to be "
                "set explicitly (no default compiler can consume CCCC "
                "dialect output)\n");
        return 1;
    }

    char *cc = cccc_find_native_cc();
    if (!cc)
        return 1;

    char *source_path = make_tmp_path(".c");
    if (!source_path) {
        fprintf(stderr, "error: failed to create native source file\n");
        free(cc);
        return 1;
    }

    char *exe_path = strdup(out_file);
    if (!exe_path) {
        fprintf(stderr, "error: failed to duplicate native output path\n");
        unlink(source_path);
        free(source_path);
        free(cc);
        return 1;
    }

    FILE *f = fopen(source_path, "w");
    if (!f) {
        fprintf(stderr, "error: failed to open %s: %s\n", source_path,
                strerror(errno));
        unlink(source_path);
        free(source_path);
        free(exe_path);
        free(cc);
        return 1;
    }
    cc_serialize_program(f, vm, prog, false, emit_test_harness);
    // #1017: cc_serialize_program() can itself queue a warning (e.g.
    // CCCC_WARN_NATIVE_NAME_COLLISION) via warn_tok()/vm->collect_errors --
    // nothing upstream of this call flushes vm->errors again, so print (and
    // clear, matching every other checkpoint's own cc_has_errors()/
    // cc_clear_errors() pairing, e.g. the one right before the -m/
    // -c=generated bail-out this function's sibling call site guards) right
    // here or it's silently dropped.
    if (cc_has_errors(vm) || vm->warning_count > 0) {
        cc_print_all_errors(vm);
        cc_clear_errors(vm);
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "error: failed to write %s: %s\n", source_path,
                strerror(errno));
        unlink(source_path);
        free(source_path);
        free(exe_path);
        free(cc);
        return 1;
    }

    ArgVec      cc_args = {0};
    StringArray owned   = {0}; // #1065: heap-backed flags freed after the spawn
    argv_push(&cc_args, cc);
    argv_push(&cc_args, source_path);
    argv_push(&cc_args, "-o");
    argv_push(&cc_args, exe_path);
    // #1073: an explicit --std= used to be forwarded byte-for-byte,
    // bypassing the ladder entirely and hitting the exact spelling
    // asymmetry it exists to route around (e.g. GCC rejects "-std=c23"
    // but accepts "-std=c2x"). Route it through the same probe, restricted
    // to spellings of the SAME standard (explicit_std=true) -- falling
    // back to the user's literal spelling, unprobed, only if none of that
    // standard's own spellings are accepted (today's pre-fix behaviour,
    // so this can never turn a working compile into a failing one).
    const char *resolved_std =
        native_resolve_std_ladder(vm, cc, std_arg != NULL);
    if (!resolved_std)
        resolved_std = std_arg;
    if (resolved_std)
        push_owned_flag(&cc_args, &owned, "-std=", resolved_std);
    // #891/#1006: cccc auto-captures each command-line input file's own
    // #include directives (preprocess.c) and re-emits them verbatim into
    // source_path, which lives in a temp directory -- so a quoted
    // `#include "local.h"` that cccc itself resolved relative to some input
    // file's own directory would otherwise be unresolvable to the native
    // compiler. Forward every input file's directory explicitly (not just
    // the primary one, #1006 widened auto-capture to every input file) so
    // re-emitted quoted includes still find it.
    NativeIncludeDirCtx incdir_ctx = {&cc_args, NULL, 0, 0};
    hashmap_foreach(&vm->compiler.command_line_inputs, push_input_dir_i,
                    &incdir_ctx);
    // #1143: a user -I/-isystem entry that also happens to hold CCCC's own
    // bundled std headers (this repo's own test harness's `-I./include` is
    // exactly that -- tools/testing/native.py) must not shadow the real
    // host headers the rest of this function relies on being reachable
    // (pthread.h's own #include_next hand-off, #1022, and everything it
    // transitively reaches) -- confirmed to otherwise collide (struct
    // sched_param/lconv/locale_t redefinitions, undeclared SIG_SETMASK/
    // htonl, static-vs-extern gethostbyname_r). cc_include_dir_is_cccc_
    // bundled() (preprocess.c) reports exactly the entries search_include_
    // paths() actually resolved one of CCCC's own bundled headers from;
    // forward those as `-idirafter` (supported by both gcc and clang, the
    // only compilers cccc_find_native_cc targets) instead of `-I`/
    // `-isystem`, so the real host header always wins the search while a
    // header the host genuinely lacks still resolves as a last resort.
    // Matches the policy man/HEADERS.md already documents ("CCCC's own
    // bundled include directory is never forwarded to the native
    // compiler") for the case that policy didn't actually cover: the
    // directory named by a *user* -I, not cccc's own builtin_include_dir.
    for (int i = 0; i < inc_paths_count; i++) {
        bool bundled = cc_include_dir_is_cccc_bundled(vm, inc_paths[i]);
        argv_push(&cc_args, bundled ? "-idirafter" : "-I");
        argv_push(&cc_args, inc_paths[i]);
    }
    for (int i = 0; i < sys_inc_paths_count; i++) {
        bool bundled = cc_include_dir_is_cccc_bundled(vm, sys_inc_paths[i]);
        argv_push(&cc_args, bundled ? "-idirafter" : "-isystem");
        argv_push(&cc_args, sys_inc_paths[i]);
    }
    for (int i = 0; i < defines_count; i++)
        push_owned_flag(&cc_args, &owned, "-D", defines[i]);
    for (int i = 0; i < undefs_count; i++)
        push_owned_flag(&cc_args, &owned, "-U", undefs[i]);
    for (int i = 0; i < lib_paths_count; i++) {
        argv_push(&cc_args, "-L");
        argv_push(&cc_args, lib_paths[i]);
    }
    for (int i = 0; i < libs_count; i++)
        push_owned_flag(&cc_args, &owned, "-l", libs[i]);
    // #1051: libm is never linked otherwise -- a guest program calling any
    // math.h function CCCC's own bundled header declares (fmaximum/
    // totalorder/etc, the whole C23 IEC 60559:2020 family CCCC implements
    // in software) fails to *link* on a host where those specific symbols
    // haven't been folded into libc itself. This stayed invisible until
    // #1037's math test hit it, because glibc >= 2.34 already folds the
    // *common* math functions (sin/sqrt/etc.) into libc.so.6, so most
    // native programs linked fine with no -lm at all. Appended last (after
    // every other -l flag) so it can satisfy an unresolved libm symbol left
    // by a preceding static archive on a linker that resolves strictly
    // left-to-right; harmless everywhere else -- an unused -lm is dropped
    // by the linker, and on a host where libm is already folded into libc
    // (glibc's newer functions aside) or into libSystem (macOS), the flag
    // is simply a no-op.
    argv_push(&cc_args, "-lm");
    // #1088: pthread is never linked otherwise -- the <threads.h> shims
    // (serialize_threads_shims, src/serialize_shims.c) call real
    // pthread_create/ pthread_mutex_*/pthread_cond_*/pthread_key_* directly,
    // same as any guest program using <pthread.h> itself. This stayed invisible
    // for pthread until now for the same reason -lm did (#1051): glibc >= 2.34
    // folds libpthread into libc.so.6 and macOS always had it in
    // libSystem, so every existing native pthread test linked fine with no
    // flag at all. An older glibc (< 2.34) needs it explicitly. Harmless
    // where unnecessary, same rationale as -lm just above.
    argv_push(&cc_args, "-pthread");
    // #1147: iconv is never linked otherwise -- <iconv.h>'s iconv_open/
    // iconv/iconv_close (include/iconv.h) are real, directly-called host
    // functions under -c=native (no CCCC wrapper, unlike the VM's own
    // FFI-resolved iconv), and macOS ships them in a separate libiconv
    // rather than folding them into libSystem the way glibc folds them
    // into libc itself -- verified directly (link fails on macOS without
    // -liconv, succeeds with it; not needed at all on Linux). Unlike -lm/
    // -pthread just above, this can't be forwarded unconditionally: glibc
    // has no separate libiconv, so an unconditional -liconv would convert
    // a macOS link gap into a Linux one. Mirrors this project's own build
    // (Makefile's Darwin-only LDFLAGS += -liconv).
#ifdef __APPLE__
    argv_push(&cc_args, "-liconv");
#endif
    // #1064: plain `char` is signed under every one of CCCC's own type
    // rules (ty_char, src/type.c), but a real host's plain `char` isn't
    // universally signed -- measured directly in the cccc-linux-arm64
    // container: glibc/aarch64 defines __CHAR_UNSIGNED__ (x86_64 does not).
    // A GNU vector_size lane read as `*((char *)&v + i)` and compared
    // against a signed constant (-1, -11, ...) silently gave the wrong
    // answer there with no compile error -- confirmed by hand-compiling
    // -m output with -funsigned-char and reproducing the ticket's exact
    // exit codes. Forwarded unconditionally and unprobed, unlike #1053's
    // -std ladder: -fsigned-char has existed in both gcc and clang for
    // decades on every target, so a run_argv_quiet() probe would only add
    // a spawn per native compile with nothing plausible to fall back to.
    // Scope: this only covers the compile -c=native drives itself -- -m/
    // -c=generated output compiled by hand still needs the flag passed
    // explicitly (documented in man/COVERAGE.md).
    argv_push(&cc_args, "-fsigned-char");
    // #1159: -O<n> used to be a hard error under -c=native (it tunes the VM's
    // own bytecode pipeline, which the native path never runs), so the host
    // cc always built at its own default (-O0 for both clang and gcc) --
    // which performs no tail-call elimination, so a deeply tail-recursive
    // guest program relying on CCCC's own VM-side TCO guarantee (CALLT,
    // -O1+) overflowed the host stack natively even though the VM itself
    // handles it in constant stack space (confirmed: identical tail-
    // recursive C via host clang segfaults at -O0, returns correctly at
    // -O2). The two concepts (VM bytecode optimization level vs. host
    // compiler optimization level) are unrelated, but reusing the same
    // -O<n> spelling for "the level the host cc should build at" is the
    // obvious, least-surprising interface -- -c=native's own admissibility
    // check (below, in main()) now lets opt_level through instead of
    // rejecting it, and forwards it here verbatim. No -O on the command
    // line means opt_level == 0 and nothing is forwarded, so the host's own
    // default (-O0) is unchanged from before this fix -- every existing
    // divergence this project documents as depending on that default (e.g.
    // __builtin_dynamic_object_size, man/COVERAGE.md) keeps its meaning.
    if (opt_level != 0) {
        char buf[2] = {(char)('0' + opt_level), '\0'};
        push_owned_flag(&cc_args, &owned, "-O", buf);
    }

    int rc = run_argv((char *const *)cc_args.data);

    unlink(source_path);
    free(cc_args.data);
    free(source_path);
    free(exe_path);
    free(cc);
    for (int i = 0; i < incdir_ctx.seen_len; i++)
        free(incdir_ctx.seen[i]);
    free(incdir_ctx.seen);
    for (int i = 0; i < owned.len; i++)
        free(owned.data[i]);
    free(owned.data);
    return rc;
}

static void print_version(void) {
    printf("cccc %s", CCCC_RELEASE_VERSION);
    if (CCCC_GIT_DESC[0])
        printf(" (%s)", CCCC_GIT_DESC);
    printf("\n");
#if defined(__aarch64__) || defined(_M_ARM64)
#define CCCC_HOST_ARCH "aarch64"
#elif defined(__x86_64__) || defined(_M_X64)
#define CCCC_HOST_ARCH "x86_64"
#else
#define CCCC_HOST_ARCH "unknown"
#endif
#if defined(__APPLE__)
#define CCCC_HOST_OS "darwin"
#elif defined(__linux__)
#define CCCC_HOST_OS "linux"
#else
#define CCCC_HOST_OS "unknown"
#endif
    printf("host: %s-%s\n", CCCC_HOST_ARCH, CCCC_HOST_OS);
    printf("features:");
#ifdef CCCC_HAS_BACKTRACE
    printf(" backtrace");
#endif
#ifdef CCCC_HAS_DECIMAL
    printf(" decimal");
#endif
#ifdef CCCC_HAS_CURL
    printf(" curl");
#endif
    printf("\n");
}

static void usage(const char *argv0, int exit_code) {
    printf("CCCC: Comprehensive C Compensation Compiler\n");
    printf("https://git.sr.ht/~takeiteasy/cccc\n\n");
    printf("Usage: %s [options] file...\n\n", argv0);
    printf("Options:\n");
    printf("\t-h/--help                Show this message\n");
    printf("\t   --version             Print version, git describe, host "
           "triple, and enabled features\n");
    printf("\t-I/--include <path>      Add <path> to include search paths\n");
    printf("\t-i/--isystem <path>      Add <path> to system include paths (for "
           "non-standard headers)\n");
    printf(
        "\t   --use-system-headers  Prefer SDK headers over CCCC polyfills for "
        "non-owned standard headers\n");
    printf("\t   --no-builtin-includes Do not fall back to CCCC's own bundled "
           "headers for non-owned standard headers (requires "
           "--use-system-headers)\n");
    printf(
        "\t   --sysroot <path>      Set SDK root; adds <path>/usr/include to "
        "system include paths and implies --use-system-headers\n");
    printf("\t-L/--library-path <path> Add <path> to dynamic library search "
           "paths\n");
    printf("\t-l/--library <name>      Link dynamic library by name or path\n");
#ifdef CCCC_HAS_CURL
    printf("\t   --url-cache-dir <path> Directory for caching #include/#embed "
           "<https://...> fetches\n");
    printf("\t   --url-cache-clear     Clear the URL fetch cache and exit\n");
    printf("\t   --url-timeout=SECONDS Set URL fetch timeout in seconds "
           "(default: 30)\n");
    printf("\t   --url-max-size=SIZE   Cap fetched URL payload size (e.g., "
           "50MB, default: 10MB)\n");
#endif
    printf("\t-D/--define <macro>[=def] Define a macro\n");
    printf("\t-U/--undef <macro>       Undefine a macro\n");
    printf("\t-a/--ast                 Dump AST\n");
    printf("\t-p/--print-tokens        Print preprocessed tokens to stdout\n");
    printf("\t-E/--preprocess          Output preprocessed source code "
           "(traditional "
           "C -E)\n");
    printf(
        "\t-m/--dump-expanded       Output macro-expanded source code (for gcc "
        "compatibility)\n");
    printf("\t   --emit-only           With -c=generated: only emit explicitly "
           "tagged "
           "content ([[cccc::emit]])\n");
    printf("\t   --attr-target=TARGET  Attribute spelling in generated output: "
           "auto, c23, gnu, msvc, strip\n");
    printf("\t   --emit-cccc           Preserve CCCC dialect syntax "
           "([[cccc::...]], @-attrs, "
           "checked-pointer\n");
    printf("\t                         qualifiers, cccc-only #includes) in "
           "-E/-m/-c=native/"
           "-c=generated output\n");
    printf("\t                         instead of stripping it to portable C. "
           "With -c=native, "
           "the usual\n");
    printf("\t                         cc/clang/gcc PATH search is disabled -- "
           "CCCC_NATIVE_CC "
           "must name a\n");
    printf("\t                         compiler that understands the dialect "
           "explicitly\n");
    printf("\t   --no-layout-guards    Suppress the _Static_assert layout "
           "guards emitted next to\n");
    printf("\t                         every aggregate definition in "
           "-m/-c=generated/-c=native\n");
    printf("\t                         output (see man/COVERAGE.md); on by "
           "default\n");
    printf("\t-j/--json                Emit JSON for all eligible output "
           "(diagnostics, header declarations, --fusion-candidates, etc.)\n");
    printf("\t-J/--ffi-decls           Emit parsed function/struct/enum "
           "declarations "
           "as JSON (for FFI wrapper generation)\n");
    printf("\t-X/--no-preprocess       Disable preprocessing step\n");
    printf("\t-S/--no-stdlib           Do not link standard library\n");
    printf("\t-c[FMT]/--compile[=FMT]  Compile only; do not execute. FMT: "
           "native (default), "
           "generated\n");
    printf("\t                         native: build a native executable via "
           "CCCC_NATIVE_CC\n");
    printf("\t                                 (cc, clang, or gcc); writes to "
           "-o file, or ./a.out\n");
    printf("\t                                 if -o omitted\n");
    printf("\t                         generated: serialize the runtime TU + "
           "macro-generated\n");
    printf("\t                                    objects to C; writes to -o "
           "file, or ./a.gen.c\n");
    printf("\t                                    if -o omitted\n");
    printf("\t                         Aliases: native=n, generated=gen=g. Use\n");
    printf("\t                         -cnative or --compile=native (short "
           "form must be\n");
    printf("\t                         attached; long form may use '=' or "
           "separate arg).\n");
    printf("\t   --test-run[=LEVEL]    Run the program under the VM "
           "(safety=max by default; LEVEL\n");
    printf("\t                         accepts none/basic/standard/max or "
           "0/1/2/3, same as --safety=)\n");
    printf("\t                         before compiling. Refuses to compile "
           "(nonzero exit, no\n");
    printf("\t                         artifact written) if the run crashes, "
           "hits a VM-detected\n");
    printf("\t                         safety violation, or hangs; the exit "
           "code itself is not\n");
    printf("\t                         checked. Implies -c=native when no -c "
           "is given; an\n");
    printf(
        "\t                         explicit -c=FMT still picks the format\n");
    printf("\t-o/--out <file>          Output file. For -c=native, defaults to "
           "./a.out if omitted.\n");
    printf("\t                         For -c=generated, defaults to "
           "./a.gen.c if omitted.\n");
    printf("\t-d/--disassemble         Disassemble compiled bytecode to "
           "stdout\n");
    printf("\t-v/--verbose             Enable debug logging\n");
    printf("\t-g/--debug               Enable interactive debugger\n");
    printf("\t   --no-debug-on-crash   Disable auto-drop into debugger on "
           "crash (for test harnesses)\n");
    printf("\t-r/--repl                Start an interactive read-eval-print "
           "loop (no input file)\n");
    printf("\t-e/--entry <name>        Set the entry-point function (default: "
           "main)\n");
    printf("\t   --vm-profile          Count executed VM opcodes and print a "
           "report\n");
    printf("\t                         Combine with --json to also dump the "
           "profile as JSON to stdout\n");
    printf("\nTesting Options:\n");
    printf("\t-t/--testing[=vm|native]\n");
    printf("\t                         Discover and run [[cccc::test]] "
           "functions. Bare -t/--testing\n");
    printf("\t                         (default =vm) runs them in-process; "
           "=native serializes the\n");
    printf("\t                         harness itself and runs\n");
    printf("\t                         it as a standalone binary via "
           "CCCC_NATIVE_CC (implies -c=native;\n");
    printf("\t                         [[cccc::test_setup/teardown]] hooks "
           "and negative tests are not\n");
    printf("\t                         supported under =native, see "
           "man/TESTING.md)\n");
    printf("\t   --test=GLOB           Run only tests whose name matches GLOB "
           "(implies --testing)\n");
    printf("\t   --test-suite=NAME     Run tests in NAME and its sub-suites "
           "(prefix match);\n");
    printf("\t                         glob metacharacters (*?[) switch to "
           "fnmatch (implies --testing)\n");
    printf("\t   --list-tests          List test names without running "
           "(implies --testing)\n");
    printf("\t   --fail-fast           Stop after the first failing test\n");
    printf("\t   --test-timeout=N      Per-test timeout in seconds (0 = no "
           "timeout;\n");
    printf("\t                         individual tests may override via\n");
    printf("\t                         [[cccc::test(timeout = ms)]])\n");
    printf("\t   --test-format=FMT     Output format for test results: tap "
           "(default), plain, json\n");
    printf("\nBuild Options:\n");
    printf("\t-b/--build               Run the input as a build script "
           "(declares native targets)\n");
    printf("\t   --build-entry=NAME    Build entry function to invoke "
           "(default: build_main)\n");
    printf("\t   --build-out-dir=PATH  Output directory for build artifacts "
           "(default: build/)\n");
    printf("\t   --build-dry-run       Print the toolchain command lines "
           "without executing them\n");
    printf("\t   --build-target=NAME   Build only the named target and its "
           "transitive dependencies\n");
    printf("\t   --build-tool-allow=N  Allowlist of tool names runnable via "
           "RunCustom/HaveTool/PkgConfig/CaptureCommand\n");
    printf("\t                         Accepts comma-separated or repeated "
           "flags. Default: allow all.\n");
    printf("\t   --build-jobs=N        Compile up to N source files in "
           "parallel per target (default: 1)\n");
    printf("\t   --build-keep-going    Continue building independent targets "
           "after a failure\n");
    printf("\t   --build-quiet         Suppress per-step command lines; only "
           "show errors and summary\n");
    printf("\t   --build-verbose       Print per-target headers and all "
           "command lines\n");
    printf("\t   --build-list-targets  List [[cccc::build_target]] factory "
           "names and exit\n");
    printf("\t   --build-profile=NAME  Set build profile: debug | release | "
           "relwithdebinfo | minsizerel\n");
    printf("\t   --build-triple=TRIPLE Cross-compile target triple (e.g. "
           "aarch64-linux-gnu; clang only)\n");
    printf("\t   --build-cc=COMPILER   Override CC binary for all targets "
           "(e.g. aarch64-linux-gnu-gcc)\n");
    printf("\t                         Env equivalent: CCCC_BUILD_CC (else "
           "cc/clang/gcc PATH search --\n");
    printf("\t                         separate from -c=native's own "
           "CCCC_NATIVE_CC)\n");
    printf("\t   --build-cache[=PATH]  Enable incremental builds: "
           "mtime+content-hash cache.\n");
    printf("\t                         Default cache dir: "
           "<out-dir>/.cccc-cache\n");
    printf("\t   --build-option=K=V    Pass a typed build option to the build "
           "script (GetBuildOption/HaveBuildOption).\n");
    printf("\t                         Accepts repeated flags: "
           "--build-option=foo=bar --build-option=baz=1\n");
    printf("\t   --build-install       After a successful build copy artifacts "
           "registered with InstallArtifact\n");
    printf("\t                         to the install prefix (default: PREFIX "
           "env var or /usr/local).\n");
    printf("\t   -- [args...]          Forward positional args to the build "
           "entry (BuildArgc/BuildArgv).\n");
    printf("\nWarning Options:\n");
    printf("\t-Wall               Enable common warning categories\n");
    printf("\t-Wextra             Enable extra warning categories\n");
    printf("\t-W<name>            Enable a warning category\n");
    printf("\t-Wno-<name>         Disable a warning category\n");
    printf("\t-w/--Werror         Treat enabled warnings as errors\n");
    printf("\t-Werror=<name>      Treat one warning category as an error\n");
    printf("\t-Wno-error=<name>   Do not promote one warning category\n");
    printf("\nSafety Levels (preset flag combinations):\n");
    printf("\t-0/--safety=none     No safety checks (VM heap stays on by "
           "default; add -V to also "
           "use the host allocator)\n");
    printf("\t-1/--safety=basic    Essential low-overhead checks (~5-10%% "
           "overhead)\n");
    printf("\t-2/--safety=standard Comprehensive development safety (~20-40%% "
           "overhead)\n");
    printf("\t-3/--safety=max      All safety features for deep debugging "
           "(~60-100%%+ overhead)\n");
    printf("\nMemory Safety Options (can be combined with safety levels):\n");
    printf("\t-B/--bounds-checks           Runtime array bounds checking\n");
    printf("\t   --checked-pointers        Runtime range checks for "
           "checked-pointer\n"
           "\t                             ([[cccc::single/array/ntarray]]) "
           "accesses\n");
    printf("\t   --uaf-detection           Use-after-free detection\n");
    printf("\t   --control-flow-integrity  Control-flow integrity (indirect "
           "call validation)\n");
    printf("\t   --type-checks             Runtime type checking on pointer "
           "dereferences\n");
    printf("\t   --uninitialized-detection Uninitialized variable detection\n");
    printf("\t   --overflow-checks         Detect signed integer overflow\n");
    printf("\t   --stack-canaries          Stack overflow protection\n");
    printf("\t   --heap-canaries           Heap overflow protection\n");
    printf("\t-M/--memory-leak-detection   Track allocations and report leaks "
           "at exit\n");
    printf("\t   --stack-instrumentation   Track stack variable lifetimes and "
           "accesses\n");
    printf("\t   --stack-errors            Enable runtime errors for stack "
           "instrumentation\n");
    printf("\t-P/--pointer-sanitizer       Enable all pointer checks (bounds, "
           "UAF, type)\n");
    printf("\t   --dangling-pointers       Detect use of stack pointers after "
           "function return\n");
    printf(
        "\t   --alignment-checks        Validate pointer alignment for type\n");
    printf("\t   --provenance-tracking     Track pointer origin and validate "
           "operations\n");
    printf("\t   --invalid-arithmetic      Detect pointer arithmetic outside "
           "object bounds\n");
    printf("\t   --format-string-checks    Validate format strings in "
           "printf-family functions\n");
    printf("\t   --random-canaries         Use random stack canaries (prevents "
           "predictable bypass)\n");
    printf("\t   --memory-poisoning        Poison allocated/freed memory "
           "(0xCD/0xDD patterns)\n");
    printf("\t   --memory-tagging          Temporal memory tagging (track "
           "pointer generation tags)\n");
    printf("\t-T/--thread-safety           Threading safety diagnostics: race "
           "detection, lock-order\n"
           "\t                             inversion, double-lock, and atomic "
           "cast warnings\n");
    printf("\t-V/--no-vm-heap             VM heap is on by default; pass -V to "
           "route malloc/free\n");
    printf("\t                             through the host allocator instead. "
           "Not compatible with\n");
    printf("\t                             -1/-2/-3 (or "
           "--safety=basic/standard/max), or with\n");
    printf("\t                             "
           "--bounds-checks/--uaf-detection/--type-checks/\n");
    printf("\t                             "
           "--heap-canaries/--memory-leak-detection/--memory-tagging,\n");
    printf("\t                             which require it\n");
    printf("\nFFI Safety Options:\n");
    printf("\t   --ffi-allow=list       Allow only comma-separated native "
           "function names\n");
    printf("\t   --ffi-deny=list        Deny comma-separated native function "
           "names\n");
    printf("\t-F/--disable-ffi          Block all registered and dynamic "
           "native calls\n");
    printf("\t   --ffi-errors-fatal     Abort execution on FFI policy "
           "violations\n");
    printf("\t   --ffi-type-checking    Validate registered FFI call arity at "
           "runtime\n");
    printf("\nLanguage Standard:\n");
    printf(
        "\t-s/--std=<std>       Select C language standard (default: gnu23)\n");
    printf("\t                     Supported: c89/c90, c99, c11, c17/c18, "
           "c23/c2x\n");
    printf("\t                     GNU variants: gnu89/gnu90, gnu99, gnu11, "
           "gnu17/gnu18, gnu23/gnu2x\n");
    printf("\t                     Gates predefined macros, tokenizer syntax "
           "(e.g. C23 "
           "attributes/\n");
    printf("\t                     digit separators), and preprocessor "
           "features per standard\n");
    printf("\nPreprocessor Options:\n");
    printf("\t   --embed-limit=SIZE         Set #embed file size warning limit "
           "(e.g., 50MB, 100mb, default: 10MB)\n");
    printf("\t   --embed-hard-limit         Make #embed limit a hard error "
           "instead of warning\n");
    printf("\t   --macro-recursion-limit=N  Limit recursive comptime macro "
           "expansion (default: 256, 0=unlimited)\n");
    printf(
        "\t-n/--max-errors=N             Cap diagnostics at N (default: 20)\n");
    printf("\t-C/--no-comptime              Skip the comptime/macro phase "
           "entirely (for\n");
    printf("\t                              large TUs that don't use "
           "[[cccc::comptime]])\n");
    printf("\t   --comptime-include-all     Forward all #define macros to the "
           "comptime pass,\n");
    printf("\t                              and widen the declaration index to "
           "include\n");
    printf("\t                              system headers (both default off; "
           "declarations\n");
    printf("\t                              from non-system headers already "
           "resolve\n");
    printf("\t                              on demand without this flag)\n");
    printf("\t   --allow-comptime-pp-bleed  Allow #define/#undef inside one\n");
    printf("\t                              [[cccc::comptime]] function body "
           "to remain\n");
    printf("\t                              visible to other comptime function "
           "bodies\n");
    printf("\t                              (pre-#283 behavior; default is "
           "isolated)\n");
    printf("\nOptimization:\n");
    printf("\t-O/--optimize[=LEVEL]        Enable bytecode optimization "
           "(default: disabled)\n");
    printf("\t                             LEVEL: 0=none, 1=basic, 2=standard, "
           "3=aggressive, 4=fused\n");
    printf("\t                             Under -c=native: forwarded "
           "verbatim as -O<n> to the host cc\n");
    printf("\t                             instead (no bytecode pipeline to "
           "optimize)\n");
    printf("\t                             1: constant folding (-ffold)\n");
    printf("\t                             2: +peephole, +CSE (-fpeephole "
           "-fcse)\n");
    printf("\t                             3: +copy-prop, +DCE (-fcopy-prop "
           "-fdce)\n");
    printf("\t                             4: +opcode fusion, +redundant "
           "extension elimination (-ffuse -felim-ext)\n");
    printf("\t-f<pass>                     Enable a single optimisation pass "
           "regardless of -O level.\n");
    printf("\t-fno-<pass>                  Disable a pass even if enabled by "
           "-O.\n");
    printf("\t                             Passes: fold, peephole, copy-prop, "
           "dce, cse, fuse, elim-ext\n");
    printf("\t                             Examples: -O3 -fno-cse, -O0 "
           "-fpeephole, "
           "-ffold -fdce\n");
    printf("\t                             Long-form aliases also accepted: "
           "--ffold, --fpeephole, "
           "--fcopy-prop,\n");
    printf("\t                             --fdce, --fcse, --ffuse, "
           "--felim-ext, and their "
           "--fno-* counterparts\n");
    printf("\t--fma                        Enable single-rounding FMA (-ffuse "
           "implied; may change FP results)\n");
    printf("\t--trap-fp-divzero            Abort on float division by zero "
           "instead of IEEE +-Inf/NaN\n");
    printf("\t--posix-emulation            Enable lossy/approximate emulation "
           "of POSIX functions the\n");
    printf("\t                             host doesn't natively support (e.g. "
           "ppoll() on macOS). Off\n");
    printf("\t                             by default: such functions are "
           "undeclared/unregistered,\n");
    printf("\t                             matching a native compiler on the "
           "same host. Also restores\n");
    printf("\t                             raw ioctl() passthrough for request "
           "codes outside the\n");
    printf("\t                             layout-verified allowlist (off by "
           "default there too). VM-only.\n");
    printf("\t--inline-limit=N             Limit inlining to N AST nodes "
           "(default: 20, 0=disable)\n");
    printf("\nStatic Bytecode Analysis (compile input, walk text "
           "segment, exit):\n");
    printf("\t--ngrams[=N]            Static opcode n-gram analysis (N=2 or 3, "
           "default 2)\n");
    printf("\t--ngrams-top=N          Show top N sequences (default 25)\n");
    printf("\t--ngrams-per-file       Print a per-input section in addition to "
           "the aggregate\n");
    printf("\t--fusion-candidates[=N] Use-def fusion candidate analysis (top "
           "N, default 50)\n");
    printf("\t                        JSON output via -j/--json\n");
    printf("\nInline Assembly:\n");
    printf("\t-A/--asm-passthru   Compile asm(\"...\") statements via native C "
           "compiler\n");
    printf("\t                    and execute them via FFI (default: no-op)\n");
    printf("\nExample:\n");
    printf("\t%s -o hello hello.c\n", argv0);
    printf("\t%s -I ./include -D DEBUG -o prog prog.c\n", argv0);
    printf("\techo 'int main() { return 42; }' | %s -\n", argv0);
    printf("\n");
    exit(exit_code);
}

static void configure_ffi_name_list(VirtualMachine *vm, const char *list,
                                    void (*add)(VirtualMachine *,
                                                const char *)) {
    const char *p = list;
    while (p && *p) {
        while (*p == ',' || isspace((unsigned char)*p))
            p++;
        const char *start = p;
        while (*p && *p != ',')
            p++;
        const char *end = p;
        while (end > start && isspace((unsigned char)end[-1]))
            end--;
        if (end > start) {
            size_t len  = (size_t)(end - start);
            char  *name = malloc(len + 1);
            if (!name)
                error("failed to allocate FFI policy name");
            memcpy(name, start, len);
            name[len] = '\0';
            add(vm, name);
            free(name);
        }
        if (*p == ',')
            p++;
    }
}

static char *find_requested_library(const char *name, const char **paths,
                                    int paths_count) {
    if (!name)
        return NULL;

    if (strchr(name, '/') || strstr(name, ".dylib") || strstr(name, ".so") ||
        strstr(name, ".dll"))
        return strdup(name);

#if defined(_WIN32) || defined(_WIN64)
    const char *prefix = "";
    const char *suffix = ".dll";
#elif defined(__APPLE__)
    const char *prefix = "lib";
    const char *suffix = ".dylib";
#else
    const char *prefix = "lib";
    const char *suffix = ".so";
#endif

    char libname[512];
    snprintf(libname, sizeof(libname), "%s%s%s", prefix, name, suffix);

    for (int i = 0; i < paths_count; i++) {
        char candidate[1024];
        snprintf(candidate, sizeof(candidate), "%s/%s", paths[i], libname);
        if (access(candidate, F_OK) == 0)
            return strdup(candidate);
    }

    return strdup(libname);
}

static int load_requested_libraries(VirtualMachine *vm, const char **libs,
                                    int libs_count, const char **paths,
                                    int paths_count) {
    for (int i = 0; i < libs_count; i++) {
        char *path = find_requested_library(libs[i], paths, paths_count);
        if (!path)
            return -1;
        int rc = cc_dlopen(vm, path);
        free(path);
#if defined(__linux__)
        // Development linker names such as libm.so may be linker scripts that
        // dlopen cannot consume. Fall back to the conventional runtime SONAME.
        if (rc != 0 && !strchr(libs[i], '/') && !strstr(libs[i], ".so")) {
            char soname[512];
            snprintf(soname, sizeof(soname), "lib%s.so.6", libs[i]);
            rc = cc_dlopen(vm, soname);
        }
#endif
        if (rc != 0)
            return -1;
    }
    return 0;
}

static int ffi_index_by_name(VirtualMachine *vm, const char *name) {
    size_t len = strlen(name);
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        if (vm->compiler.ffi_table[i].name &&
            vm->compiler.ffi_table[i].name_len == len &&
            memcmp(vm->compiler.ffi_table[i].name, name, len) == 0)
            return i;
    }
    return -1;
}

static const char *obj_external_name(Obj *obj) {
    return obj && obj->asm_label ? obj->asm_label : obj ? obj->name : NULL;
}

static int count_params(Type *ty) {
    int n = 0;
    for (Type *p = ty; p; p = p->next)
        n++;
    return n;
}

static void register_dynamic_externs(VirtualMachine *vm, Obj *prog) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        const char *extern_name = obj_external_name(obj);
        if (!obj->is_function || obj->is_definition || !extern_name ||
            ffi_index_by_name(vm, extern_name) >= 0)
            continue;

        int nargs          = count_params(obj->ty->params);
        int returns_double = is_flonum(obj->ty->return_ty);
        if (obj->ty->is_variadic)
            cc_register_variadic_cfunc(vm, extern_name, (void *)1, nargs,
                                       returns_double);
        else
            cc_register_cfunc(vm, extern_name, (void *)1, nargs,
                              returns_double);
        vm->compiler.ffi_table[vm->compiler.ffi_count - 1]
            .is_dynamic_placeholder = 1;
    }
}

static int verify_dynamic_externs(VirtualMachine *vm) {
    int ok = 1;
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        ForeignFunc *ff = &vm->compiler.ffi_table[i];
        if (ff->is_dynamic_placeholder &&
            (!ff->func_ptr || ff->func_ptr == (void *)1)) {
            fprintf(stderr, "error: unresolved dynamic library symbol '%s'\n",
                    ff->name);
            ok = 0;
        }
    }
    return ok ? 0 : -1;
}

static char *read_stdin_to_tmp(void) {
#if defined(_WIN32)
    char  tmpPath[MAX_PATH + 1];
    char  tmpFile[MAX_PATH + 1];
    DWORD len = GetTempPathA(MAX_PATH, tmpPath);
    if (len == 0 || len > MAX_PATH)
        return NULL;
    if (GetTempFileNameA(tmpPath, "asi", 0, tmpFile) == 0)
        return NULL;
    HANDLE h =
        CreateFileA(tmpFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    char   buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        DWORD written = 0;
        if (!WriteFile(h, buf, (DWORD)n, &written, NULL) ||
            written != (DWORD)n) {
            CloseHandle(h);
            DeleteFileA(tmpFile);
            return NULL;
        }
    }
    if (ferror(stdin)) {
        CloseHandle(h);
        DeleteFileA(tmpFile);
        return NULL;
    }
    CloseHandle(h);
    return _strdup(tmpFile);
#else
    char template[] = "/tmp/cccc-stdin-XXXXXX";
    int  fd         = mkstemp(template);
    if (fd < 0)
        return NULL;
    char    buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        ssize_t w = 0;
        while (w < n) {
            ssize_t m = write(fd, buf + w, n - w);
            if (m < 0) {
                close(fd);
                unlink(template);
                return NULL;
            }
            w += m;
        }
    }
    if (n < 0) {
        close(fd);
        unlink(template);
        return NULL;
    }
    if (close(fd) < 0) {
        unlink(template);
        return NULL;
    }
    return strdup(template);
#endif
}

static void parse_define(VirtualMachine *vm, char *arg) {
    // #1065: used to split in place (`*eq = '\0'`), permanently truncating
    // the `defines[]` entry this is called on at its '=' -- harmless the
    // first time (cc_define()/define_macro() copy what they're given), but
    // that same `defines[]` array is reused later by run_native_backend()
    // to build -D flags for the host cc, which then saw a bare "-DA"
    // instead of "-DA=1" for every defines entry with a value. Split via a
    // bounded copy instead so the original string survives for that later
    // reuse.
    char *eq = strchr(arg, '=');
    if (eq) {
        size_t name_len = (size_t)(eq - arg);
        char  *name     = malloc(name_len + 1);
        if (!name)
            error("failed to allocate -D name");
        memcpy(name, arg, name_len);
        name[name_len] = '\0';
        cc_define(vm, name, eq + 1);
        free(name);
    } else
        cc_define(vm, arg, "1");
}

static size_t parse_size(const char *str, const char *flag_name) {
    char  *endptr;
    double value = strtod(str, &endptr);

    if (value < 0) {
        fprintf(stderr, "error: %s must be non-negative\n", flag_name);
        exit(1);
    }

    size_t multiplier = 1;
    if (*endptr != '\0') {
        // Parse suffix (KB, MB, GB, etc.)
        if (strcasecmp(endptr, "kb") == 0 || strcasecmp(endptr, "k") == 0) {
            multiplier = 1024;
        } else if (strcasecmp(endptr, "mb") == 0 ||
                   strcasecmp(endptr, "m") == 0) {
            multiplier = 1024 * 1024;
        } else if (strcasecmp(endptr, "gb") == 0 ||
                   strcasecmp(endptr, "g") == 0) {
            multiplier = 1024 * 1024 * 1024;
        } else if (strcasecmp(endptr, "b") == 0) {
            multiplier = 1; // Bytes
        } else {
            fprintf(stderr,
                    "error: invalid size suffix '%s' for %s (use KB, MB, GB, "
                    "or B)\n",
                    endptr, flag_name);
            exit(1);
        }
    }

    return (size_t)(value * multiplier);
}

static void parse_warning_option(const char *arg, uint64_t *warnings,
                                 uint64_t *warning_errors,
                                 uint64_t *warning_no_errors,
                                 uint64_t *warning_sticky_errors,
                                 int      *warnings_as_errors) {
    if (strcmp(arg, "error") == 0) {
        *warnings_as_errors = 1;
        *warning_no_errors  = 0;
        return;
    }

    if (strncmp(arg, "error=", 6) == 0) {
        const char *name = arg + 6;
        uint64_t    mask = cccc_warning_mask_for_name(name);
        if (!mask || cccc_warning_is_group_name(name)) {
            fprintf(stderr, "error: unknown warning option '-Werror=%s'\n",
                    name);
            exit(1);
        }
        *warnings              |= mask;
        *warning_errors        |= mask;
        *warning_no_errors     &= ~mask;
        *warning_sticky_errors |= mask;
        return;
    }

    if (strncmp(arg, "no-error=", 9) == 0) {
        const char *name = arg + 9;
        uint64_t    mask = cccc_warning_mask_for_name(name);
        if (!mask || cccc_warning_is_group_name(name)) {
            fprintf(stderr, "error: unknown warning option '-Wno-error=%s'\n",
                    name);
            exit(1);
        }
        // -Werror=<name> is sticky: a later -Wno-error=<name> cannot demote it.
        // Use -Wno-<name> to fully disable (clearing the sticky bit too).
        if (!(*warning_sticky_errors & mask)) {
            *warning_errors    &= ~mask;
            *warning_no_errors |= mask;
        }
        return;
    }

    bool        disable = false;
    const char *name    = arg;
    if (strncmp(arg, "no-", 3) == 0) {
        disable = true;
        name    = arg + 3;
    }

    uint64_t mask = cccc_warning_mask_for_name(name);
    if (!mask) {
        fprintf(stderr, "error: unknown warning option '-W%s'\n", arg);
        exit(1);
    }

    if (disable) {
        *warnings              &= ~mask;
        *warning_errors        &= ~mask;
        *warning_no_errors     &= ~mask;
        *warning_sticky_errors &= ~mask;
    } else {
        *warnings |= mask;
    }
}

static char **build_source_argv(int *prog_argc, int argc, const char *argv[],
                                int optind, int dashdash) {
    int prog_start;
    if (dashdash >= 0) {
        prog_start = dashdash + 1;
        *prog_argc = argc - dashdash; // argv[0] + everything after "--"
    } else {
        prog_start = optind;
        *prog_argc = argc - optind + 1;
    }

    char **prog_argv = malloc(sizeof(char *) * (size_t)*prog_argc);
    if (!prog_argv)
        error("out of memory");
    prog_argv[0] = (char *)argv[0];
    for (int i = 1; i < *prog_argc; i++)
        prog_argv[i] = (char *)argv[prog_start + i - 1];
    return prog_argv;
}

// VM-only runtime flags (safety instrumentation + debug support) and their
// CLI spelling, for the "-c=native"/"-m"/"-c=generated ignores these" warning
// below (#924). Deliberately excludes CCCC_VM_HEAP (forcing the VM-managed heap
// allocator is meaningless outside the VM -- it was never one of these
// bits) and CCCC_FFI_ERRORS_FATAL (already has its own dedicated hard error
// in the "-c=native cannot be combined with CCCC FFI policy options" check,
// keyed off the ffi_errors_fatal bool rather than this flags word -- listing
// it here too would claim it's merely "ignored" when it actually still
// errors).
static const struct {
    uint32_t    bit;
    const char *name;
} ignored_vm_flag_names[] = {
    {CCCC_BOUNDS_CHECKS, "--bounds-checks"},
    {CCCC_CHECKED_BOUNDS, "--checked-pointers"},
    {CCCC_UAF_DETECTION, "--uaf-detection"},
    {CCCC_TYPE_CHECKS, "--type-checks"},
    {CCCC_UNINIT_DETECTION, "--uninitialized-detection"},
    {CCCC_OVERFLOW_CHECKS, "--overflow-checks"},
    {CCCC_STACK_CANARIES, "--stack-canaries"},
    {CCCC_HEAP_CANARIES, "--heap-canaries"},
    {CCCC_MEMORY_LEAK_DETECT, "--memory-leak-detection"},
    {CCCC_STACK_INSTR, "--stack-instrumentation"},
    {CCCC_DANGLING_DETECT, "--dangling-pointers"},
    {CCCC_ALIGNMENT_CHECKS, "--alignment-checks"},
    {CCCC_PROVENANCE_TRACK, "--provenance-tracking"},
    {CCCC_INVALID_ARITH, "--invalid-arithmetic"},
    {CCCC_STACK_INSTR_ERRORS, "--stack-errors"},
    {CCCC_FORMAT_STR_CHECKS, "--format-string-checks"},
    {CCCC_RANDOM_CANARIES, "--random-canaries"},
    {CCCC_MEMORY_POISONING, "--memory-poisoning"},
    {CCCC_MEMORY_TAGGING, "--memory-tagging"},
    {CCCC_CFI, "--control-flow-integrity"},
    {CCCC_ENABLE_DEBUGGER, "--debug"},
    {CCCC_THREAD_SAFETY, "--thread-safety"},
    {CCCC_NO_DEBUG_ON_CRASH, "--no-debug-on-crash"},
    {CCCC_FMA, "--fma"},
    {CCCC_TRAP_FP_DIVZERO, "--trap-fp-divzero"},
};

// Prints "warning: <context> ignores VM runtime safety/debug options
// (--a, --b, ...): they are enforced by the CCCC VM only" for every bit of
// `flags` that names a VM-only feature, then returns `flags` with those
// bits cleared -- the caller passes the result on instead of the flags
// enforcement can't actually apply to (#924: -c=native/-m/-c=generated used
// to hard error here; now they warn and continue, matching how a real C
// compiler treats an option it can't honour).
static uint32_t warn_ignored_vm_flags(uint32_t flags, const char *context) {
    uint32_t relevant = 0;
    for (size_t i = 0;
         i < sizeof(ignored_vm_flag_names) / sizeof(ignored_vm_flag_names[0]);
         i++)
        relevant |= ignored_vm_flag_names[i].bit;
    if (!(flags & relevant))
        return flags;
    fprintf(stderr, "warning: %s ignores VM runtime safety/debug options (",
            context);
    bool first = true;
    for (size_t i = 0;
         i < sizeof(ignored_vm_flag_names) / sizeof(ignored_vm_flag_names[0]);
         i++) {
        if (!(flags & ignored_vm_flag_names[i].bit))
            continue;
        fprintf(stderr, "%s%s", first ? "" : ", ",
                ignored_vm_flag_names[i].name);
        first = false;
    }
    fprintf(stderr, "): they are enforced by the CCCC VM only\n");
    return flags & ~relevant;
}

int main(int argc, const char *argv[]) {
    /* Initialise and install libbacktrace crash handler as early as possible
     * so that any fault during parse/codegen/VM produces a symbolic host
     * C stack trace to stderr before the process dies. */
    cc_host_backtrace_init(argv[0]);
    cc_host_backtrace_install_fatal();

    int          exit_code           = 0;
    const char **input_files         = NULL;
    int          input_files_count   = 0;
    Obj **volatile input_progs       = NULL;
    Token **volatile input_tokens    = NULL;
    const char **inc_paths           = NULL; // -I
    int          inc_paths_count     = 0;
    const char **sys_inc_paths       = NULL; // -isystem
    int          sys_inc_paths_count = 0;
    const char **lib_paths           = NULL; // -L / --library-path
    int          lib_paths_count     = 0;
    const char **libs                = NULL; // --library
    int          libs_count          = 0;
    const char **defines             = NULL; // -D
    int          defines_count       = 0;
    const char **undefs              = NULL; // -U
    int          undefs_count        = 0;
    char        *out_file            = NULL; // -o (single)
    int          dump_ast            = 0;    // -a
    int          disassemble         = 0;    // -d
    int          verbose             = 0;    // -v
    uint32_t flags = CCCC_VM_HEAP; // CCCCFlags bitfield for runtime features;
                                   // VM heap is on by default (#665)
    uint32_t cli_flags_mask = 0;   // Bits explicitly set via CLI; wins over
                                   // #pragma cccc config(...) (#357)
    bool safety_level_gt0 =
        false; // True if -1/-2/-3 or --safety=basic/standard/max was requested
               // (#665)
    bool vm_heap_disable_requested =
        false; // True if -V/--no-vm-heap was passed (now toggles the heap off)
               // (#665)
    bool cli_opt_level_set =
        false; // True if -O/--optimize was passed on the CLI (#357)
    int print_tokens        = 0;   // -p
    int preprocess_only     = 0;   // -E
    int dump_expanded_only  = 0;   // -m
    int emit_generated_only = 0;   // -c=generated
    int emit_only           = 0;   // --emit-only
    int skip_preprocess     = 0;   // -X
    int skip_stdlib         = 0;   // -S
    int output_json         = 0;   // -j (general "emit JSON" flag)
    int output_ffi_decls    = 0;   // -J/--ffi-decls
#ifdef CCCC_HAS_CURL
    char  *url_cache_dir   = NULL; // --url-cache-dir
    int    url_cache_clear = 0;    // --url-cache-clear
    int    url_timeout     = 0;    // --url-timeout (0 = use default)
    size_t url_max_size    = 0;    // --url-max-size (0 = use default)
#endif
    CCCCAttrTarget attr_target    = CCCC_ATTR_TARGET_AUTO; // --attr-target
    int            emit_cccc_mode = 0;                     // --emit-cccc
    int            no_layout_guards_mode = 0; // --no-layout-guards (#1172)
    int            test_run_mode         = 0; // --test-run[=LEVEL]
    uint64_t       test_run_flags =
        0; // safety preset bits for --test-run's VM smoke test
    int compile_only =
        0; // -c (set whenever -c/--compile is given; semantics:
           //   "compile, do not execute". -c=native hands off to the
           //   system compiler.)
    int      max_errors         = 20; // --max-errors (default: 20)
    int      warnings_as_errors = 0;  // -Werror / --Werror
    uint64_t warnings           = 0;
    uint64_t warning_errors     = 0;
    uint64_t warning_no_errors  = 0;
    uint64_t warning_sticky_errors =
        0; // bits pinned by -Werror=<name>; resist -Wno-error=<name>
    size_t embed_limit           = 0;  // --embed-limit (0 = use default)
    int    embed_hard_error      = 0;  // --embed-hard-limit
    int    macro_recursion_limit = -1; // --macro-recursion-limit
    int    repl_mode             = 0;  // -r / --repl
    int    opt_level = 0; // -O0/-O1/-O2/-O3/-O4 (default: 0 = no optimization)
    int    ffp_contract_fma = 0; // --fma
    uint32_t opt_f_enable   = 0; // passes forced ON  by -f<pass>
    uint32_t opt_f_disable  = 0; // passes forced OFF by -fno-<pass>
    uint32_t opt_f_mask     = 0; // all bits touched by -f/-fno- CLI args (#612)
    int      inline_node_limit  = 20; // --inline-limit (default 20, 0=disable)
    int      asm_passthru       = 0;  // --asm-passthru
    const char  *std_arg        = NULL; // --std=<standard>
    const char **ffi_allow_args = NULL;
    int          ffi_allow_args_count     = 0;
    const char **ffi_deny_args            = NULL;
    int          ffi_deny_args_count      = 0;
    int          disable_all_ffi          = 0;
    int          ffi_errors_fatal         = 0;
    int          enable_ffi_type_checking = 0;
    int          vm_profile               = 0;
    int          vm_profile_text          = 0;
    const char  *vm_profile_mode          = NULL;
    const char  *vm_profile_input         = NULL;
    int          vm_profile_ran           = 0;
    const char  *entry_name               = NULL; // -e / --entry
    enum { COMPILE_NONE, COMPILE_NATIVE, COMPILE_GENERATED } compile_format =
        COMPILE_NONE;
    int            no_comptime             = 0; // --no-comptime / -C
    int            comptime_include_all    = 0; // --comptime-include-all
    int            allow_comptime_pp_bleed = 0; // --allow-comptime-pp-bleed
    int            run_ngrams = 0; // 0 = off; 2 or 3 = enabled with n-gram size
    int            ngrams_top = 25;
    int            ngrams_per_file = 0;
    int            run_fusion      = 0; // 0 = off; >0 = enabled, value is top-N
    CcNgramState  *ngram_state     = NULL;
    CcFusionState *fusion_state    = NULL;
    int testing_mode = 0; // any --testing[=...]/--test*/--list-tests flag
    // --testing[=vm|native]: bare -t/--testing defaults to VM (today's
    // behaviour, unchanged); =native drives a serialized-harness -c=native
    // round-trip (#1033).
    enum { TESTING_BACKEND_VM, TESTING_BACKEND_NATIVE } testing_backend =
        TESTING_BACKEND_VM;
    const char  *test_glob     = NULL;            // --test=GLOB
    const char  *suite_filter  = NULL;            // --test-suite=NAME
    int          list_tests    = 0;               // --list-tests
    int          fail_fast     = 0;               // --fail-fast
    int          test_timeout  = 0;               // --test-timeout=N
    CcTestFormat test_format   = TEST_FORMAT_TAP; // --test-format=FORMAT
    int          build_mode    = 0;               // --build
    const char  *build_entry   = NULL;            // --build-entry=NAME
    const char  *build_target  = NULL;            // --build-target=NAME
    const char  *build_out_dir = NULL; // --build-out-dir=PATH (default "build")
    int          build_dry_run = 0;    // --build-dry-run
    int          build_verbose = 0;    // --build-verbose
    int          build_quiet   = 0;    // --build-quiet
    int          build_keep_going       = 0;    // --build-keep-going
    int          build_jobs             = 1;    // --build-jobs=N
    int          build_list_targets     = 0;    // --build-list-targets (#540)
    const char  *build_profile          = NULL; // --build-profile=NAME (#548)
    const char  *build_triple           = NULL; // --build-triple=TRIPLE (#547)
    const char  *build_cc               = NULL; // --build-cc=COMPILER (#547)
    const char  *build_cache            = NULL; // --build-cache[=PATH] (#546)
    const char **build_tool_allow       = NULL; // --build-tool-allow=name,...
    int          build_tool_allow_count = 0;
    const char **build_options       = NULL;  // --build-option=key=value (#559)
    int          build_options_count = 0;
    int          build_install       = 0;     // --build-install (#560)
    bool         use_system_headers  = false; // --use-system-headers
    bool         no_builtin_includes = false; // --no-builtin-includes
    const char  *sysroot             = NULL;  // --sysroot <path>

    if (argc <= 1)
        usage(argv[0], 1);

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"out", required_argument, 0, 'o'},
        {"disassemble", no_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},
        {"ast", no_argument, 0, 'a'},
        {"print-tokens", no_argument, 0, 'p'},
        {"preprocess", no_argument, 0, 'E'},
        {"dump-expanded", no_argument, 0, 'm'},
        {"no-preprocess", no_argument, 0, 'X'},
        {"no-stdlib", no_argument, 0, 'S'},
        {"json", no_argument, 0, 'j'},
        {"ffi-decls", no_argument, 0, 'J'},
        {"compile", optional_argument, 0, 'c'},
        {"debug", no_argument, 0, 'g'},
        {"safety", required_argument, 0, 1012},
        {"bounds-checks", no_argument, 0, 'B'},
        {"checked-pointers", no_argument, 0, 1119},
        {"emit-cccc", no_argument, 0, 1120},
        {"test-run", optional_argument, 0, 1121},
        {"no-layout-guards", no_argument, 0, 1122},
        {"uaf-detection", no_argument, 0, 1078},
        {"type-checks", no_argument, 0, 1079},
        {"uninitialized-detection", no_argument, 0, 1038},
        {"overflow-checks", no_argument, 0, 1034},
        {"stack-canaries", no_argument, 0, 1039},
        {"heap-canaries", no_argument, 0, 1080},
        {"pointer-sanitizer", no_argument, 0, 'P'},
        {"memory-leak-detection", no_argument, 0, 'M'},
        {"stack-instrumentation", no_argument, 0, 1043},
        {"stack-errors", no_argument, 0, 1005},
        {"dangling-pointers", no_argument, 0, 1001},
        {"alignment-checks", no_argument, 0, 1002},
        {"provenance-tracking", no_argument, 0, 1003},
        {"invalid-arithmetic", no_argument, 0, 1004},
        {"format-string-checks", no_argument, 0, 1044},
        {"random-canaries", no_argument, 0, 1081},
        {"memory-poisoning", no_argument, 0, 1007},
        {"memory-tagging", no_argument, 0, 1045},
        {"no-vm-heap", no_argument, 0, 'V'},
        {"control-flow-integrity", no_argument, 0, 1111},
        {"no-comptime", no_argument, 0, 'C'},
        {"thread-safety", no_argument, 0, 'T'},
        {"include", required_argument, 0, 'I'},
        {"isystem", required_argument, 0, 'i'},
        {"library-path", required_argument, 0, 'L'},
        {"library", required_argument, 0, 'l'},
        {"define", required_argument, 0, 'D'},
        {"undef", required_argument, 0, 'U'},
        {"url-cache-dir", required_argument, 0, 1008},
        {"url-cache-clear", no_argument, 0, 1009},
        {"url-timeout", required_argument, 0, 1010},
        {"url-max-size", required_argument, 0, 1011},
        {"max-errors", required_argument, 0, 'n'},
        {"Werror", no_argument, 0, 'w'},
        {"embed-limit", required_argument, 0, 1048},
        {"embed-hard-limit", no_argument, 0, 1060},
        {"optimize", optional_argument, 0, 'O'},
        {"fma", no_argument, 0, 1072},
        {"posix-emulation", no_argument, 0, 1117},
        {"macro-recursion-limit", required_argument, 0, 1115},
        {"repl", no_argument, 0, 'r'},
        {"std", required_argument, 0, 's'},
        {"ffi-allow", required_argument, 0, 1052},
        {"ffi-deny", required_argument, 0, 1053},
        {"disable-ffi", no_argument, 0, 'F'},
        {"ffi-errors-fatal", no_argument, 0, 1055},
        {"ffi-type-checking", no_argument, 0, 1024},
        {"vm-profile", no_argument, 0, 1056},
        {"entry", required_argument, 0, 'e'},
        {"ngrams", optional_argument, 0, 1057},
        {"ngrams-top", required_argument, 0, 1030},
        {"ngrams-per-file", no_argument, 0, 1031},
        {"fusion-candidates", optional_argument, 0, 1058},
        {"comptime-include-all", no_argument, 0, 1050},
        {"allow-comptime-pp-bleed", no_argument, 0, 1068},
        {"inline-limit", required_argument, 0, 1051},
        {"asm-passthru", no_argument, 0, 'A'},
        {"testing", optional_argument, 0, 't'},
        {"test", required_argument, 0, 1061},
        {"test-suite", required_argument, 0, 1062},
        {"list-tests", no_argument, 0, 1063},
        {"fail-fast", no_argument, 0, 1064},
        {"test-timeout", required_argument, 0, 1065},
        {"test-format", required_argument, 0, 1066},
        {"emit-only", no_argument, 0, 1067},
        {"attr-target", required_argument, 0, 1069},
        {"no-debug-on-crash", no_argument, 0, 1071},
        {"build", no_argument, 0, 'b'},
        {"build-entry", required_argument, 0, 1074},
        {"build-out-dir", required_argument, 0, 1075},
        {"build-dry-run", no_argument, 0, 1076},
        {"build-target", required_argument, 0, 1077},
        {"build-tool-allow", required_argument, 0, 1082},
        {"build-jobs", required_argument, 0, 1083},
        {"build-keep-going", no_argument, 0, 1084},
        {"build-quiet", no_argument, 0, 1085},
        {"build-verbose", no_argument, 0, 1086},
        {"build-list-targets", no_argument, 0, 1087},
        {"build-profile", required_argument, 0, 1088},
        {"build-triple", required_argument, 0, 1089},
        {"build-cc", required_argument, 0, 1090},
        {"build-cache", optional_argument, 0, 1091},
        {"build-option", required_argument, 0, 1092},
        {"build-install", no_argument, 0, 1093},
        // Per-pass optimisation enables/disables (long-form aliases for
        // -f<pass>)
        {"ffold", no_argument, 0, 1096},
        {"fpeephole", no_argument, 0, 1097},
        {"fcopy-prop", no_argument, 0, 1098},
        {"fdce", no_argument, 0, 1099},
        {"fcse", no_argument, 0, 1100},
        {"ffuse", no_argument, 0, 1101},
        {"fno-fold", no_argument, 0, 1102},
        {"fno-peephole", no_argument, 0, 1103},
        {"fno-copy-prop", no_argument, 0, 1104},
        {"fno-dce", no_argument, 0, 1105},
        {"fno-cse", no_argument, 0, 1106},
        {"fno-fuse", no_argument, 0, 1107},
        {"felim-ext", no_argument, 0, 1108},
        {"fno-elim-ext", no_argument, 0, 1109},
        // System-header mode
        {"use-system-headers", no_argument, 0, 1112},
        {"no-builtin-includes", no_argument, 0, 1113},
        {"sysroot", required_argument, 0, 1114},
        {"trap-fp-divzero", no_argument, 0, 1116},
        {"version", no_argument, 0, 1118},
        {0, 0, 0, 0}};

    // Find "--" separator: args after it are forwarded to the compiled program
    int dashdash = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            dashdash = i;
            break;
        }
    }
    int         getopt_argc = (dashdash >= 0) ? dashdash : argc;
    const char *optstring =
        "0123haI:L:D:U:o:c::dvgi:PEMXSjJVCl:W:e:O::Fbt::Tmpn:rs:ABf:w";
    int opt;
    opterr = 0; // we'll handle errors explicitly
    while ((opt = getopt_long(getopt_argc, (char *const *)argv, optstring,
                              long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                usage(argv[0], 0);
                break;
            case 1118: // --version
                print_version();
                exit(0);
            case '0':
                // Safety level 0: None - explicitly clear all safety flags, but
                // VM heap stays on by default (#665); use -V to turn it off
                // too.
                flags             = CCCC_VM_HEAP;
                safety_level_gt0  = false;
                cli_flags_mask   |= CCCC_SAFETY_PRESET_BITS;
                break;
            case '1':
                // Safety level 1: Basic - essential low-overhead checks
                flags            |= CCCC_SAFETY_BASIC;
                safety_level_gt0  = true;
                cli_flags_mask   |= CCCC_SAFETY_PRESET_BITS;
                break;
            case '2':
                // Safety level 2: Standard - comprehensive development safety
                flags            |= CCCC_SAFETY_STANDARD;
                safety_level_gt0  = true;
                cli_flags_mask   |= CCCC_SAFETY_PRESET_BITS;
                break;
            case '3':
                // Safety level 3: Maximum - all safety features
                flags            |= CCCC_SAFETY_MAX;
                safety_level_gt0  = true;
                cli_flags_mask   |= CCCC_SAFETY_PRESET_BITS;
                break;
            case 1012:
                // --safety=<level> flag
                if (strncmp(optarg, "none", sizeof("none")) == 0 ||
                    strncmp(optarg, "0", sizeof("0")) == 0) {
                    flags            = CCCC_VM_HEAP;
                    safety_level_gt0 = false;
                } else if (strncmp(optarg, "basic", sizeof("basic")) == 0 ||
                           strncmp(optarg, "1", sizeof("1")) == 0) {
                    flags            |= CCCC_SAFETY_BASIC;
                    safety_level_gt0  = true;
                } else if (strncmp(optarg, "standard", sizeof("standard")) ==
                               0 ||
                           strncmp(optarg, "2", sizeof("2")) == 0) {
                    flags            |= CCCC_SAFETY_STANDARD;
                    safety_level_gt0  = true;
                } else if (strncmp(optarg, "max", sizeof("max")) == 0 ||
                           strncmp(optarg, "3", sizeof("3")) == 0) {
                    flags            |= CCCC_SAFETY_MAX;
                    safety_level_gt0  = true;
                } else {
                    fprintf(stderr,
                            "error: invalid safety level '%s' (use "
                            "none/basic/standard/max or 0/1/2/3)\n",
                            optarg);
                    usage(argv[0], 1);
                }
                cli_flags_mask |= CCCC_SAFETY_PRESET_BITS;
                break;
            case 'o':
                if (out_file) {
                    fprintf(stderr, "error: only one -o/--out allowed\n");
                    usage(argv[0], 1);
                }
                out_file = strdup(optarg);
                break;
            case 'e':
                entry_name = optarg;
                break;
            case 'd':
                disassemble = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'a':
                dump_ast = 1;
                break;
            case 'g':
                flags |= CCCC_ENABLE_DEBUGGER;
                break;
            case 'I':
                inc_paths = realloc(inc_paths,
                                    sizeof(*inc_paths) * (inc_paths_count + 1));
                inc_paths[inc_paths_count++] = strdup(optarg);
                break;
            case 'L':
                lib_paths = realloc(lib_paths,
                                    sizeof(*lib_paths) * (lib_paths_count + 1));
                lib_paths[lib_paths_count++] = strdup(optarg);
                break;
            case 'i': // --isystem
                sys_inc_paths =
                    realloc(sys_inc_paths,
                            sizeof(*sys_inc_paths) * (sys_inc_paths_count + 1));
                sys_inc_paths[sys_inc_paths_count++] = strdup(optarg);
                break;
            case 'l': // --library
                libs = realloc(libs, sizeof(*libs) * (libs_count + 1));
                libs[libs_count++] = strdup(optarg);
                break;
            case 'D':
                defines =
                    realloc(defines, sizeof(*defines) * (defines_count + 1));
                defines[defines_count++] = strdup(optarg);
                break;
            case 'U':
                undefs = realloc(undefs, sizeof(*undefs) * (undefs_count + 1));
                undefs[undefs_count++] = strdup(optarg);
                break;
            case 'B': // --bounds-checks
                flags          |= CCCC_BOUNDS_CHECKS;
                cli_flags_mask |= CCCC_BOUNDS_CHECKS;
                break;
            case 1119: // --checked-pointers (#770/#482-484)
                // Deliberately not "--checked-bounds" -- too easily confused
                // with -B/--bounds-checks above, which is a different,
                // allocation-size-derived check (CHKB). Deliberately not in
                // CCCC_ALL_SAFETY/any -0.._3 preset either (see
                // CCCC_CHECKED_BOUNDS's comment in cccc.h): checked-pointer
                // types and their compile-time arithmetic rules are always on,
                // but CHKR is opt-in only.
                flags          |= CCCC_CHECKED_BOUNDS;
                cli_flags_mask |= CCCC_CHECKED_BOUNDS;
                break;
            case 1078: // --uaf-detection
                flags          |= CCCC_UAF_DETECTION;
                cli_flags_mask |= CCCC_UAF_DETECTION;
                break;
            case 1079: // --type-checks
                flags          |= CCCC_TYPE_CHECKS;
                cli_flags_mask |= CCCC_TYPE_CHECKS;
                break;
            case 1038: // --uninitialized-detection
                flags |= CCCC_UNINIT_DETECTION;
                break;
            case 1034: // --overflow-checks
                flags          |= CCCC_OVERFLOW_CHECKS;
                cli_flags_mask |= CCCC_OVERFLOW_CHECKS;
                break;

            case 1039: // --stack-canaries
                flags          |= CCCC_STACK_CANARIES;
                cli_flags_mask |= CCCC_STACK_CANARIES;
                break;
            case 1080: // --heap-canaries
                flags          |= CCCC_HEAP_CANARIES;
                cli_flags_mask |= CCCC_HEAP_CANARIES;
                break;
            case 'P': // --pointer-sanitizer
                flags          |= CCCC_POINTER_SANITIZER;
                cli_flags_mask |= CCCC_POINTER_SANITIZER;
                break;
            case 'M': // --memory-leak-detection
                flags          |= CCCC_MEMORY_LEAK_DETECT;
                cli_flags_mask |= CCCC_MEMORY_LEAK_DETECT;
                break;
            case 1043: // --stack-instrumentation
                flags |= CCCC_STACK_INSTR;
                break;
            case 1001:
                flags |= CCCC_DANGLING_DETECT;
                break;
            case 1002:
                flags |= CCCC_ALIGNMENT_CHECKS;
                break;
            case 1003:
                flags |= CCCC_PROVENANCE_TRACK;
                break;
            case 1004:
                flags |= CCCC_INVALID_ARITH;
                break;
            case 1005:
                flags |= CCCC_STACK_INSTR_ERRORS;
                break;
            case 1044: // --format-string-checks
                flags    |= CCCC_FORMAT_STR_CHECKS;
                warnings |= CCCC_WARN_FORMAT;
                break;
            case 1081: // --random-canaries
                flags |= CCCC_RANDOM_CANARIES;
                break;
            case 1007:
                flags |= CCCC_MEMORY_POISONING;
                break;
            case 1045: // --memory-tagging
                flags          |= CCCC_MEMORY_TAGGING;
                cli_flags_mask |= CCCC_MEMORY_TAGGING;
                break;
            case 'T': // --thread-safety
                flags |= CCCC_THREAD_SAFETY;
                break;
            case 'V':
                // VM heap is on by default (#665); -V/--no-vm-heap now toggles
                // it off. Resolved after the option loop since it must see the
                // final safety level regardless of flag order.
                vm_heap_disable_requested = true;
                break;
            case 'C':
                no_comptime = 1;
                break;
            case 1111: // --control-flow-integrity (long form only)
                flags |= CCCC_CFI;
                break;
            case 'p':
                print_tokens = 1;
                break;
            case 'E':
                preprocess_only = 1;
                break;
            case 'm':
                dump_expanded_only = 1;
                break;
            case 'X':
                skip_preprocess = 1;
                break;
            case 'S':
                skip_stdlib = 1;
                break;
            case 'j':
                output_json = 1;
                break;
            case 'c': { // -c[FMT]/--compile[=FMT]
                // Bare -c / --compile defaults to native. The optional
                // argument selects the format. Note: GNU getopt's `::` only
                // supports attached form for short options (e.g. `-cnative`),
                // not `-c=native`. The long form accepts `--compile=native`
                // and `--compile native` (the latter as a separate arg). Strip
                // a leading `=` to be friendly to BSD getopt / `-c=native`
                // callers even though GNU's getopt rejects that form outright.
                const char *fmt = optarg;
                if (fmt && fmt[0] == '=')
                    fmt++;
                if (!fmt || !*fmt) {
                    compile_format = COMPILE_NATIVE;
                    compile_only   = 1;
                } else if (strcmp(fmt, "native") == 0 ||
                           strcmp(fmt, "n") == 0) {
                    compile_format = COMPILE_NATIVE;
                    compile_only   = 1;
                } else if (strcmp(fmt, "generated") == 0 ||
                           strcmp(fmt, "gen") == 0 || strcmp(fmt, "g") == 0) {
                    // -c=generated (#936): folds the old standalone -G/
                    // --emit-generated into the -c namespace. Unlike native
                    // this does NOT set compile_only -- it reuses the
                    // dump_expanded_only/emit_generated_only serialization path
                    // below (same as -m), which historically has never been
                    // gated by compile_only. Flipping compile_only here would
                    // change behavior at every site that branches on it (the
                    // --repl/--build/--ngrams validation blocks, etc.) for no
                    // reason -- -c=generated is a serialize-and-exit mode, not
                    // a "hand off to another backend" mode like native is.
                    // compile_format is only consulted here to pick
                    // -c=generated's default output filename.
                    compile_format      = COMPILE_GENERATED;
                    dump_expanded_only  = 1;
                    emit_generated_only = 1;
                } else {
                    fprintf(stderr,
                            "error: invalid --compile format '%s' "
                            "(use 'native' or 'generated')\n",
                            fmt);
                    usage(argv[0], 1);
                }
                break;
            }
#ifdef CCCC_HAS_CURL
            case 1008:
                url_cache_dir = strdup(optarg);
                break;
            case 1009:
                url_cache_clear = 1;
                break;
            case 1010: { // --url-timeout
                char *end;
                long  secs = strtol(optarg, &end, 10);
                if (*end != '\0' || secs <= 0 || secs > 24L * 60 * 60) {
                    fprintf(stderr, "error: --url-timeout expects a positive "
                                    "number of seconds (max 86400)\n");
                    exit(1);
                }
                url_timeout = (int)secs;
                break;
            }
            case 1011: // --url-max-size
                url_max_size = parse_size(optarg, "--url-max-size");
                break;
#endif
            case 'n': // --max-errors
                max_errors = atoi(optarg);
                if (max_errors <= 0) {
                    fprintf(stderr,
                            "error: --max-errors must be a positive integer\n");
                    usage(argv[0], 1);
                }
                break;
            case 'w': // --Werror
                warnings_as_errors = 1;
                warning_no_errors  = 0;
                break;
            case 'W':
                parse_warning_option(optarg, &warnings, &warning_errors,
                                     &warning_no_errors, &warning_sticky_errors,
                                     &warnings_as_errors);
                break;
            case 1048: // --embed-limit
                embed_limit = parse_size(optarg, "--embed-limit");
                break;
            case 1060: // --embed-hard-limit
                embed_hard_error = 1;
                break;
            case 'O':  // --optimize / -O (also matches --optimize via
                       // long_options)
                if (optarg == NULL) {
                    // Just -O or --optimize without argument means -O1
                    opt_level = 1;
                } else if (optarg[0] >= '0' && optarg[0] <= '4' &&
                           optarg[1] == '\0') {
                    opt_level = optarg[0] - '0';
                } else {
                    fprintf(
                        stderr,
                        "error: invalid optimization level '%s' (use 0, 1, 2, "
                        "3, or 4)\n",
                        optarg);
                    usage(argv[0], 1);
                }
                cli_opt_level_set = true;
                break;
            case 1072: // --fma
                opt_f_enable     |= CCCC_OPT_FUSE;
                ffp_contract_fma  = 1;
                flags            |= CCCC_FMA;
                break;
            case 1117:   // --posix-emulation
                flags |= CCCC_POSIX_EMULATION;
                break;
            case 'r':    // --repl
                repl_mode = 1;
                break;
            case 1115: { // --macro-recursion-limit
                char *end = NULL;
                long  val = strtol(optarg, &end, 10);
                if (!optarg[0] || *end != '\0' || val < 0 || val > INT32_MAX) {
                    fprintf(stderr, "error: --macro-recursion-limit must be a "
                                    "non-negative integer\n");
                    usage(argv[0], 1);
                }
                macro_recursion_limit = (int)val;
                break;
            }
            case 's':  // --std=<standard>
                std_arg = optarg;
                break;
            case 1052: // --ffi-allow
                ffi_allow_args =
                    realloc(ffi_allow_args, sizeof(*ffi_allow_args) *
                                                (ffi_allow_args_count + 1));
                ffi_allow_args[ffi_allow_args_count++] = strdup(optarg);
                break;
            case 1053: // --ffi-deny
                ffi_deny_args =
                    realloc(ffi_deny_args,
                            sizeof(*ffi_deny_args) * (ffi_deny_args_count + 1));
                ffi_deny_args[ffi_deny_args_count++] = strdup(optarg);
                break;
            case 'F':  // --disable-ffi
                disable_all_ffi = 1;
                break;
            case 1055: // --ffi-errors-fatal
                ffi_errors_fatal  = 1;
                flags            |= CCCC_FFI_ERRORS_FATAL;
                break;
            case 1116: // --trap-fp-divzero
                flags          |= CCCC_TRAP_FP_DIVZERO;
                cli_flags_mask |= CCCC_TRAP_FP_DIVZERO;
                break;
            case 1024:
                enable_ffi_type_checking = 1;
                break;
            case 1056: // --vm-profile
                vm_profile      = 1;
                vm_profile_text = 1;
                break;
            case 1057: { // --ngrams[=N]
                if (optarg == NULL) {
                    run_ngrams = 2;
                } else if (optarg[0] >= '0' && optarg[0] <= '9' &&
                           optarg[1] == '\0') {
                    run_ngrams = optarg[0] - '0';
                } else {
                    fprintf(stderr,
                            "error: invalid --ngrams value '%s' (use 2 or 3)\n",
                            optarg);
                    usage(argv[0], 1);
                }
                if (run_ngrams != 2 && run_ngrams != 3) {
                    fprintf(stderr, "error: --ngrams must be 2 or 3 (got %d)\n",
                            run_ngrams);
                    usage(argv[0], 1);
                }
                break;
            }
            case 1030: { // --ngrams-top=N
                char *end = NULL;
                long  val = strtol(optarg, &end, 10);
                if (!optarg[0] || *end != '\0' || val <= 0 || val > INT32_MAX) {
                    fprintf(stderr,
                            "error: --ngrams-top must be a positive integer\n");
                    usage(argv[0], 1);
                }
                ngrams_top = (int)val;
                break;
            }
            case 1031:   // --ngrams-per-file
                ngrams_per_file = 1;
                break;
            case 1050:   // --comptime-include-all
                comptime_include_all = 1;
                break;
            case 1051: { // --inline-limit
                char *end = NULL;
                long  val = strtol(optarg, &end, 10);
                if (!optarg[0] || *end != '\0' || val < 0 || val > INT32_MAX) {
                    fprintf(stderr, "error: --inline-limit must be a "
                                    "non-negative integer\n");
                    usage(argv[0], 1);
                }
                inline_node_limit = (int)val;
                break;
            }
            case 'A': // --asm-passthru
                asm_passthru = 1;
                break;
            case 't': // --testing[=vm|native]
                testing_mode = 1;
                if (optarg) {
                    if (strcmp(optarg, "vm") == 0) {
                        testing_backend = TESTING_BACKEND_VM;
                    } else if (strcmp(optarg, "native") == 0) {
                        testing_backend = TESTING_BACKEND_NATIVE;
                    } else {
                        fprintf(stderr,
                                "error: --testing: unknown backend '%s' "
                                "(expected vm or native)\n",
                                optarg);
                        usage(argv[0], 1);
                    }
                }
                break;
            case 1061: // --test=GLOB
                test_glob    = optarg;
                testing_mode = 1;
                break;
            case 1062: // --test-suite=NAME
                suite_filter = optarg;
                testing_mode = 1;
                break;
            case 1063: // --list-tests
                list_tests   = 1;
                testing_mode = 1;
                break;
            case 1064: // --fail-fast
                fail_fast    = 1;
                testing_mode = 1;
                break;
            case 1065: // --test-timeout=N
                test_timeout = atoi(optarg);
                testing_mode = 1;
                break;
            case 1067: // --emit-only
                emit_only = 1;
                break;
            case 1068: // --allow-comptime-pp-bleed
                allow_comptime_pp_bleed = 1;
                break;
            case 1069: // --attr-target=auto|c23|gnu|msvc|strip
                if (strcmp(optarg, "auto") == 0) {
                    attr_target = CCCC_ATTR_TARGET_AUTO;
                } else if (strcmp(optarg, "c23") == 0) {
                    attr_target = CCCC_ATTR_TARGET_C23;
                } else if (strcmp(optarg, "gnu") == 0) {
                    attr_target = CCCC_ATTR_TARGET_GNU;
                } else if (strcmp(optarg, "msvc") == 0) {
                    attr_target = CCCC_ATTR_TARGET_MSVC;
                } else if (strcmp(optarg, "strip") == 0) {
                    attr_target = CCCC_ATTR_TARGET_STRIP;
                } else {
                    fprintf(stderr,
                            "error: invalid --attr-target '%s' "
                            "(use 'auto', 'c23', 'gnu', 'msvc', or 'strip')\n",
                            optarg);
                    usage(argv[0], 1);
                }
                break;
            case 1120:   // --emit-cccc
                emit_cccc_mode = 1;
                break;
            case 1122:   // --no-layout-guards (#1172)
                no_layout_guards_mode = 1;
                break;
            case 1121: { // --test-run[=LEVEL]
                test_run_mode     = 1;
                const char *level = optarg;
                if (level && level[0] == '=')
                    level++;
                if (!level || !*level || strcmp(level, "max") == 0 ||
                    strcmp(level, "3") == 0) {
                    test_run_flags = CCCC_SAFETY_MAX;
                } else if (strcmp(level, "none") == 0 ||
                           strcmp(level, "0") == 0) {
                    test_run_flags = CCCC_VM_HEAP;
                } else if (strcmp(level, "basic") == 0 ||
                           strcmp(level, "1") == 0) {
                    test_run_flags = CCCC_SAFETY_BASIC;
                } else if (strcmp(level, "standard") == 0 ||
                           strcmp(level, "2") == 0) {
                    test_run_flags = CCCC_SAFETY_STANDARD;
                } else {
                    fprintf(stderr,
                            "error: invalid --test-run level '%s' (use "
                            "none/basic/standard/max or 0/1/2/3)\n",
                            level);
                    usage(argv[0], 1);
                }
                break;
            }
            case 1071: // --no-debug-on-crash
                flags |= CCCC_NO_DEBUG_ON_CRASH;
                break;
            case 'b':  // --build
                build_mode = 1;
                break;
            case 1074: // --build-entry=NAME
                build_entry = optarg;
                build_mode  = 1;
                break;
            case 1075: // --build-out-dir=PATH
                build_out_dir = optarg;
                build_mode    = 1;
                break;
            case 1076: // --build-dry-run
                build_dry_run = 1;
                build_mode    = 1;
                break;
            case 1077: // --build-target=NAME
                build_target = optarg;
                build_mode   = 1;
                break;
            case 1082: { // --build-tool-allow=name[,name,...]
                // Accept comma-separated names:
                // --build-tool-allow=cc,ar,pkg-config or repeated flags:
                // --build-tool-allow=cc --build-tool-allow=ar
                char *tmp     = strdup(optarg);
                char *saveptr = NULL;
                char *tok     = strtok_r(tmp, ",", &saveptr);
                while (tok) {
                    build_tool_allow = realloc(
                        build_tool_allow, sizeof(*build_tool_allow) *
                                              (build_tool_allow_count + 1));
                    build_tool_allow[build_tool_allow_count++] = strdup(tok);
                    tok = strtok_r(NULL, ",", &saveptr);
                }
                free(tmp);
                build_mode = 1;
                break;
            }
            case 1083: { // --build-jobs=N
                int n = atoi(optarg);
                if (n < 1) {
                    fprintf(
                        stderr,
                        "error: --build-jobs requires a positive integer\n");
                    usage(argv[0], 1);
                }
                build_jobs = n;
                build_mode = 1;
                break;
            }
            case 1084: // --build-keep-going
                build_keep_going = 1;
                build_mode       = 1;
                break;
            case 1085: // --build-quiet
                build_quiet = 1;
                build_mode  = 1;
                break;
            case 1086: // --build-verbose
                build_verbose = 1;
                build_mode    = 1;
                break;
            case 1087: // --build-list-targets
                build_list_targets = 1;
                build_mode         = 1;
                break;
            case 1088: // --build-profile=NAME
                build_profile = optarg;
                build_mode    = 1;
                break;
            case 1089: // --build-triple=TRIPLE
                build_triple = optarg;
                build_mode   = 1;
                break;
            case 1090: // --build-cc=COMPILER
                build_cc   = optarg;
                build_mode = 1;
                break;
            case 1091: // --build-cache[=PATH]
                build_cache = optarg ? optarg : "";
                build_mode  = 1;
                break;
            case 1092: { // --build-option=key[=value]
                const char **tmp =
                    realloc(build_options,
                            (build_options_count + 1) * sizeof(*build_options));
                if (!tmp) {
                    fprintf(stderr, "error: out of memory\n");
                    return 1;
                }
                build_options                        = tmp;
                build_options[build_options_count++] = optarg;
                build_mode                           = 1;
                break;
            }
            case 1093: // --build-install
                build_install = 1;
                build_mode    = 1;
                break;
            case 1066: // --test-format=FORMAT
                if (strcmp(optarg, "tap") == 0) {
                    test_format = TEST_FORMAT_TAP;
                } else if (strcmp(optarg, "plain") == 0) {
                    test_format = TEST_FORMAT_PLAIN;
                } else if (strcmp(optarg, "json") == 0) {
                    test_format = TEST_FORMAT_JSON;
                } else {
                    fprintf(stderr,
                            "error: invalid --test-format '%s' "
                            "(use 'tap', 'plain', or 'json')\n",
                            optarg);
                    usage(argv[0], 1);
                }
                testing_mode = 1;
                break;
            case 1058: { // --fusion-candidates[=N]
                if (optarg == NULL) {
                    run_fusion = 1;
                } else {
                    char *end = NULL;
                    long  val = strtol(optarg, &end, 10);
                    if (!optarg[0] || *end != '\0' || val <= 0 ||
                        val > INT32_MAX) {
                        fprintf(stderr,
                                "error: --fusion-candidates top-N must be a "
                                "positive integer\n");
                        usage(argv[0], 1);
                    }
                    run_fusion = (int)val;
                }
                break;
            }
            case 'f': { // -f<pass> / -fno-<pass>  (e.g. -ffold, -fno-cse)
                static const struct {
                    const char *name;
                    CcccOptPass bit;
                } pass_table[]      = {{"fold", CCCC_OPT_FOLD},
                                       {"peephole", CCCC_OPT_PEEPHOLE},
                                       {"copy-prop", CCCC_OPT_COPY_PROP},
                                       {"dce", CCCC_OPT_DCE},
                                       {"cse", CCCC_OPT_CSE},
                                       {"fuse", CCCC_OPT_FUSE},
                                       {"elim-ext", CCCC_OPT_ELIM_EXT},
                                       {NULL, 0}};
                bool        neg     = (strncmp(optarg, "no-", 3) == 0);
                const char *name    = neg ? optarg + 3 : optarg;
                bool        matched = false;
                for (int k = 0; pass_table[k].name; k++) {
                    if (strcmp(name, pass_table[k].name) == 0) {
                        if (neg)
                            opt_f_disable |= (uint32_t)pass_table[k].bit;
                        else
                            opt_f_enable |= (uint32_t)pass_table[k].bit;
                        opt_f_mask |= (uint32_t)pass_table[k].bit;
                        matched     = true;
                        break;
                    }
                }
                if (!matched) {
                    fprintf(stderr,
                            "error: unknown optimisation pass '-f%s'\n"
                            "       valid: fold, peephole, copy-prop, dce, "
                            "cse, fuse "
                            "(prefix 'no-' to disable)\n",
                            optarg);
                    usage(argv[0], 1);
                }
                break;
            }
            case 'J':
                output_ffi_decls = 1;
                break;
            case 1096:
                opt_f_enable |= CCCC_OPT_FOLD;
                opt_f_mask   |= CCCC_OPT_FOLD;
                break; // --ffold
            case 1097:
                opt_f_enable |= CCCC_OPT_PEEPHOLE;
                opt_f_mask   |= CCCC_OPT_PEEPHOLE;
                break; // --fpeephole
            case 1098:
                opt_f_enable |= CCCC_OPT_COPY_PROP;
                opt_f_mask   |= CCCC_OPT_COPY_PROP;
                break; // --fcopy-prop
            case 1099:
                opt_f_enable |= CCCC_OPT_DCE;
                opt_f_mask   |= CCCC_OPT_DCE;
                break; // --fdce
            case 1100:
                opt_f_enable |= CCCC_OPT_CSE;
                opt_f_mask   |= CCCC_OPT_CSE;
                break; // --fcse
            case 1101:
                opt_f_enable |= CCCC_OPT_FUSE;
                opt_f_mask   |= CCCC_OPT_FUSE;
                break; // --ffuse
            case 1102:
                opt_f_disable |= CCCC_OPT_FOLD;
                opt_f_mask    |= CCCC_OPT_FOLD;
                break; // --fno-fold
            case 1103:
                opt_f_disable |= CCCC_OPT_PEEPHOLE;
                opt_f_mask    |= CCCC_OPT_PEEPHOLE;
                break; // --fno-peephole
            case 1104:
                opt_f_disable |= CCCC_OPT_COPY_PROP;
                opt_f_mask    |= CCCC_OPT_COPY_PROP;
                break; // --fno-copy-prop
            case 1105:
                opt_f_disable |= CCCC_OPT_DCE;
                opt_f_mask    |= CCCC_OPT_DCE;
                break; // --fno-dce
            case 1106:
                opt_f_disable |= CCCC_OPT_CSE;
                opt_f_mask    |= CCCC_OPT_CSE;
                break; // --fno-cse
            case 1107:
                opt_f_disable |= CCCC_OPT_FUSE;
                opt_f_mask    |= CCCC_OPT_FUSE;
                break; // --fno-fuse
            case 1108:
                opt_f_enable |= CCCC_OPT_ELIM_EXT;
                opt_f_mask   |= CCCC_OPT_ELIM_EXT;
                break; // --felim-ext
            case 1109:
                opt_f_disable |= CCCC_OPT_ELIM_EXT;
                opt_f_mask    |= CCCC_OPT_ELIM_EXT;
                break; // --fno-elim-ext
            case 1112:
                use_system_headers = true;
                break; // --use-system-headers
            case 1113:
                no_builtin_includes = true;
                break; // --no-builtin-includes
            case 1114:
                sysroot            = optarg;
                use_system_headers = true;
                break; // --sysroot
            case '?':
                if (optopt)
                    fprintf(stderr, "error: option -%c requires an argument\n",
                            optopt);
                else if (optind > 0 && argv[optind - 1] &&
                         argv[optind - 1][0] == '-')
                    fprintf(stderr, "error: unknown option %s\n",
                            argv[optind - 1]);
                else
                    fprintf(stderr, "error: unknown parsing error\n");
                usage(argv[0], 1);
                break;
            default:
                usage(argv[0], 1);
        }
    }

    // Resolve -V/--no-vm-heap now that the final safety level is known (#665).
    // VM heap is required by -1/-2/-3, so disabling it there is a hard error;
    // at level 0 (default or explicit -0) -V just turns the default off.
    //
    // It is also required by any individual flag in CCCC_VM_HEAP_TRIGGERS
    // (bounds/UAF/type checks, heap canaries, leak detection, memory
    // tagging): all of them key off AllocHeader metadata that only exists
    // for VM-heap allocations. Without this check, -V + one of these flags
    // silently produces a program where the requested check never fires
    // instead of an error -- e.g. -V --bounds-checks segfaults on the exact
    // out-of-bounds write it was asked to catch, rather than trapping it
    // (#845).
    if (vm_heap_disable_requested) {
        if (safety_level_gt0) {
            fprintf(stderr,
                    "error: -V/--no-vm-heap cannot be combined with -1/-2/-3 "
                    "(or --safety=basic/standard/max); those levels require "
                    "the VM heap\n");
            usage(argv[0], 1);
        }
        uint32_t vm_heap_dependent =
            flags & (CCCC_VM_HEAP_TRIGGERS & ~(uint32_t)CCCC_VM_HEAP);
        if (vm_heap_dependent) {
            fprintf(stderr, "error: -V/--no-vm-heap cannot be combined with "
                            "--bounds-checks/--uaf-detection/--type-checks/"
                            "--heap-canaries/--memory-leak-detection or memory "
                            "tagging; those checks require the VM heap\n");
            usage(argv[0], 1);
        }
        flags          &= ~CCCC_VM_HEAP;
        cli_flags_mask |= CCCC_VM_HEAP;
    }

    /* Remaining arguments are input files (positional, up to "--" if present)
     */
    for (int i = optind; i < getopt_argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "-", sizeof("-")) == 0) {
            input_files = realloc(input_files, sizeof(*input_files) *
                                                   (input_files_count + 1));
            input_files[input_files_count++] = strdup("-");
        } else {
            input_files = realloc(input_files, sizeof(*input_files) *
                                                   (input_files_count + 1));
            input_files[input_files_count++] = strdup(a);
        }
    }
    // If no input files, error (the REPL is the one mode that runs without one)
    if (input_files_count == 0 && !repl_mode) {
        fprintf(stderr, "error: no input files\n");
        usage((char *)argv[0], 1);
    }

    if (test_run_mode) {
        // --test-run runs a VM smoke test before compiling, so it needs a
        // real compile/run pipeline to slot into -- reject every mode that
        // has no such pipeline (mirrors the --repl/--build validation
        // blocks). --testing is its own compile-then-run-tests pipeline;
        // combining the two is redundant, so it's rejected too rather than
        // silently picking one.
        if (repl_mode || build_mode || testing_mode || run_ngrams ||
            run_fusion || disassemble || preprocess_only ||
            dump_expanded_only || print_tokens || output_json ||
            output_ffi_decls || dump_ast) {
            fprintf(stderr,
                    "error: --test-run cannot be combined with --repl, "
                    "--build, --testing, --ngrams/--fusion-candidates, -d, "
                    "-E, -m, --ast, -j, -J, or other output modes\n");
            usage(argv[0], 1);
        }
        // Bare -c already means native; --test-run mirrors that default
        // when no explicit -c=FMT was given.
        if (compile_format == COMPILE_NONE)
            compile_format = COMPILE_NATIVE;
        compile_only = 1;
        // These bits must survive into cc_init() below so the VM smoke-test
        // phase actually runs with the requested safety instrumentation
        // baked into codegen -- unlike a plain -c=native, whose serializer
        // reconstructs C from the AST and never looks at vm.flags, so
        // carrying these bits costs the eventual native artifact nothing.
        flags |= test_run_flags;
    }

    if (testing_backend == TESTING_BACKEND_NATIVE) {
        // --testing=native serializes the [[cccc::test]] harness itself and
        // hands off to the host toolchain (#1033) -- it's a compile-then-run
        // pipeline in the same spirit as --test-run/-c=native, not the
        // in-process cc_run_tests path --testing=vm uses.
        if (build_mode || repl_mode || test_run_mode ||
            (compile_format != COMPILE_NONE &&
             compile_format != COMPILE_NATIVE) ||
            disassemble || preprocess_only || dump_expanded_only ||
            print_tokens || output_json || output_ffi_decls || dump_ast) {
            fprintf(stderr,
                    "error: --testing=native cannot be combined with "
                    "--build, --repl, --test-run, -c=generated, "
                    "-d, -E, -m, --ast, -j, -J, or other output modes\n");
            usage(argv[0], 1);
        }
        compile_format = COMPILE_NATIVE;
        compile_only   = 1;
    }

    if (repl_mode) {
        // --repl is an interactive VM-only mode: it takes no input files and
        // is mutually exclusive with every other frontend/output/execution
        // mode (mirrors the --build validation block below).
        if (input_files_count != 0) {
            fprintf(stderr, "error: --repl does not take input files\n");
            usage(argv[0], 1);
        }
        if (build_mode || testing_mode || run_ngrams || run_fusion ||
            compile_format != COMPILE_NONE || disassemble || preprocess_only ||
            dump_expanded_only || print_tokens || output_json ||
            output_ffi_decls || dump_ast || vm_profile) {
            fprintf(stderr,
                    "error: --repl cannot be combined with --build, --testing, "
                    "--ngrams/--fusion-candidates, -c (incl. -c=native), -d, "
                    "-E, -m, --ast, --vm-profile, or other output modes\n");
            usage(argv[0], 1);
        }
    }

    if (build_mode) {
        // --build runs the build script in the VM; the host runner compiles the
        // declared targets. VM-only and output modes do not apply here.
        if (compile_format != COMPILE_NONE || disassemble || opt_level != 0 ||
            opt_f_enable || opt_f_disable || vm_profile || out_file ||
            preprocess_only || dump_expanded_only || print_tokens ||
            output_json || output_ffi_decls || dump_ast) {
            fprintf(stderr,
                    "error: --build cannot be combined with VM/output options "
                    "(-c, -d, -O<n>, -f<pass>, --vm-profile, -o, -E, -m, "
                    "--ast, ...)\n");
            usage(argv[0], 1);
        }
        if (flags & CCCC_ENABLE_DEBUGGER) {
            fprintf(stderr, "error: --build cannot be combined with --debug\n");
            usage(argv[0], 1);
        }
    }

    if (compile_format == COMPILE_NATIVE) {
        if (preprocess_only || dump_expanded_only || print_tokens ||
            output_json || output_ffi_decls || dump_ast) {
            fprintf(stderr, "error: -c=native cannot be combined with frontend "
                            "output modes\n");
            usage(argv[0], 1);
        }
        // #1159: -O<n> used to be rejected here alongside the other VM
        // bytecode-pipeline-only options -- but under -c=native it no longer
        // means "optimize CCCC's own bytecode" (there is no bytecode; the
        // native path never runs one), so run_native_backend() now repurposes
        // it as the level to forward to the host cc instead of erroring.
        // -f<pass>/-d/--entry/--vm-profile stay rejected: they genuinely only
        // make sense against the VM's own pipeline, with no host-cc
        // equivalent to repurpose them as.
        if (disassemble || entry_name || opt_f_enable || opt_f_disable ||
            vm_profile) {
            fprintf(stderr, "error: -c=native cannot be combined with VM "
                            "bytecode options\n");
            usage(argv[0], 1);
        }
        // #924: VM-only safety/debug flags used to be a hard error here.
        // -c=native hands off to the host toolchain and genuinely cannot
        // enforce them (there is no equivalent of e.g. CHKR in the
        // serialized C), but that's true of every other mode that can't
        // honour a runtime-only flag too -- warn and drop the bits instead
        // of refusing to compile, consistent with 1b/1c below. Skipped
        // under --test-run: those bits are for the VM smoke-test phase, not
        // the native compile, and the native serializer works from the AST
        // (never looks at vm.flags), so carrying them costs nothing and the
        // warning would be spurious noise about a flag that's genuinely in
        // effect.
        if (!test_run_mode)
            flags = warn_ignored_vm_flags(flags, "-c=native");
        if (ffi_allow_args_count || ffi_deny_args_count || disable_all_ffi ||
            ffi_errors_fatal || enable_ffi_type_checking) {
            fprintf(stderr, "error: -c=native cannot be combined with CCCC FFI "
                            "policy options\n");
            usage(argv[0], 1);
        }
        if (!out_file) {
            // Match cc/clang/gcc's a.out convention: -c=native with no -o
            // builds ./a.out instead of erroring.
            out_file = strdup("a.out");
        }
    }

    if (run_ngrams || run_fusion) {
        // Static analysis is mutually exclusive with execution / output modes.
        if (run_ngrams && run_fusion) {
            fprintf(
                stderr,
                "error: --ngrams and --fusion-candidates cannot be combined\n");
            usage(argv[0], 1);
        }
        if (preprocess_only || dump_expanded_only || print_tokens ||
            output_json || output_ffi_decls || dump_ast) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with frontend output modes\n");
            usage(argv[0], 1);
        }
        if (compile_only || disassemble || out_file || entry_name) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with VM bytecode output or entry options\n");
            usage(argv[0], 1);
        }
        if (vm_profile) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with --vm-profile\n");
            usage(argv[0], 1);
        }
        if (flags & CCCC_ENABLE_DEBUGGER) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with -g/--debug\n");
            usage(argv[0], 1);
        }
        if (flags != 0) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with VM runtime safety options\n");
            usage(argv[0], 1);
        }
        if (compile_format == COMPILE_NATIVE) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with -c=native\n");
            usage(argv[0], 1);
        }
    }

    // Auto-enable the interactive debugger when running interactively, so a
    // fatal runtime error traps into the debugger instead of just exiting
    // (ticket #405). Excluded for --testing/--test/--test-suite since those
    // fork child processes (src/tests.c) that inherit the parent's TTY, and
    // an external test harness is expected to pass --no-debug-on-crash
    // itself if it invokes cccc directly.
    bool auto_debug_on_crash =
        !(flags & CCCC_ENABLE_DEBUGGER) && !(flags & CCCC_NO_DEBUG_ON_CRASH) &&
        !testing_mode && !build_mode && CCCC_ISATTY(CCCC_FILENO(stdin)) &&
        CCCC_ISATTY(CCCC_FILENO(stdout));
    if (auto_debug_on_crash)
        flags |= CCCC_ENABLE_DEBUGGER;

    VirtualMachine vm;
    cc_init(&vm, flags);
    if (auto_debug_on_crash)
        vm.dbg.crash_debug_auto = true;
    vm.compiler.cli_flags_mask    = cli_flags_mask;
    vm.compiler.cli_opt_level_set = cli_opt_level_set;
    vm.compiler.native_mode       = (compile_format == COMPILE_NATIVE);
    vm.compiler.compile_only      = compile_only;
    vm.compiler.asm_passthru            = asm_passthru;
    vm.compiler.no_comptime             = no_comptime;
    vm.compiler.comptime_include_all    = comptime_include_all;
    vm.compiler.allow_comptime_pp_bleed = allow_comptime_pp_bleed;
    vm.compiler.emit_strict             = emit_only;
    vm.compiler.attr_target             = attr_target;
    vm.compiler.emit_cccc               = (bool)emit_cccc_mode;
    vm.compiler.no_layout_guards        = (bool)no_layout_guards_mode;
    vm.compiler.entry_name              = (char *)entry_name;
    vm.compiler.testing_mode            = (bool)testing_mode;
    vm.compiler.build_mode              = (bool)build_mode;
    init_mode_macros(&vm);
    vm.compiler.diagnostic_json = output_json;
    vm.disable_all_ffi          = disable_all_ffi;
    vm.ffi_errors_fatal         = ffi_errors_fatal;
    vm.enable_ffi_type_checking = enable_ffi_type_checking;
    vm.vm_profile_enabled       = vm_profile;
    if (vm_profile) {
        vm.vm_profile_trigram_counts =
            calloc((size_t)OP_COUNT * OP_COUNT * OP_COUNT, sizeof(uint64_t));
        // Failure is non-fatal: trigram section will be skipped in JSON output
    }
    for (int i = 0; i < ffi_allow_args_count; i++)
        configure_ffi_name_list(&vm, ffi_allow_args[i], cc_ffi_allow);
    for (int i = 0; i < ffi_deny_args_count; i++)
        configure_ffi_name_list(&vm, ffi_deny_args[i], cc_ffi_deny);

    if (verbose)
        vm.debug_vm = 1;

    if (repl_mode) {
        // Interactive read-eval-print loop: persistent VM, no input file.
        // See src/repl.c for the session driver.
        if (macro_recursion_limit >= 0)
            vm.compiler.macro_recursion_limit = macro_recursion_limit;
        cc_run_repl(&vm);
        goto BAIL;
    }

    // If the only input file is "-", read stdin into a temporary file and
    // replace it
    if (input_files_count == 1 &&
        strncmp(input_files[0], "-", sizeof("-")) == 0) {
        char *tmp = read_stdin_to_tmp();
        if (!tmp) {
            fprintf(stderr,
                    "error: failed to read stdin into temporary file\n");
            exit_code = 1;
            goto BAIL;
        }
        free((void *)input_files[0]);
        input_files[0] = tmp;
    }

    // Configure #embed limits if specified
    if (embed_limit > 0) {
        vm.compiler.embed_limit = embed_limit;
        vm.compiler.embed_hard_limit =
            embed_limit; // Use same value for both warnings
    }
    if (embed_hard_error) {
        vm.compiler.embed_hard_error = true;
    }

    // Set optimization level and per-pass overrides
    vm.compiler.opt_level         = opt_level;
    vm.compiler.ffp_contract_fma  = ffp_contract_fma;
    vm.compiler.opt_f_enable      = opt_f_enable;
    vm.compiler.opt_f_disable     = opt_f_disable;
    vm.compiler.cli_f_mask        = opt_f_mask;
    vm.compiler.inline_node_limit = inline_node_limit;
    if (macro_recursion_limit >= 0)
        vm.compiler.macro_recursion_limit = macro_recursion_limit;

    // Apply --std=<standard> if specified, then re-emit std macros
    if (std_arg) {
        CStdVersion ver    = CCCC_STD_C17;
        bool        is_gnu = true;
        const char *s      = std_arg;
        // Consume optional "gnu" / "c" prefix
        if (strncmp(s, "gnu", 3) == 0) {
            is_gnu  = true;
            s      += 3;
        } else if (s[0] == 'c') {
            is_gnu  = false;
            s      += 1;
        } else {
            fprintf(stderr, "error: unknown C standard '%s'\n", std_arg);
            usage(argv[0], 1);
        }
        // Map suffix to version
        if (strcmp(s, "89") == 0 || strcmp(s, "90") == 0) {
            ver = CCCC_STD_C89;
        } else if (strcmp(s, "99") == 0) {
            ver = CCCC_STD_C99;
        } else if (strcmp(s, "11") == 0) {
            ver = CCCC_STD_C11;
        } else if (strcmp(s, "17") == 0 || strcmp(s, "18") == 0) {
            ver = CCCC_STD_C17;
        } else if (strcmp(s, "23") == 0 || strcmp(s, "2x") == 0) {
            ver = CCCC_STD_C23;
        } else {
            fprintf(stderr, "error: unknown C standard '%s'\n", std_arg);
            usage(argv[0], 1);
        }
        vm.compiler.c_std     = ver;
        vm.compiler.c_std_gnu = is_gnu;
        define_std_macros(&vm);
    }

    // If random canaries are enabled, regenerate the stack canary
    if (vm.flags & CCCC_RANDOM_CANARIES) {
        vm.stack_canary = generate_random_canary();
    }

    // Enable error collection for better error reporting
#ifdef CCCC_HAS_CURL
    if (url_cache_dir) {
        vm.compiler.url_cache_dir = url_cache_dir;
    }
    if (url_cache_clear) {
        clear_url_cache(&vm);
    }
    if (url_timeout > 0) {
        vm.compiler.url_timeout = url_timeout;
    }
    if (url_max_size > 0) {
        vm.compiler.url_max_size = url_max_size;
    }
#endif

    vm.collect_errors     = true;
    vm.max_errors         = max_errors;
    vm.warnings_as_errors = warnings_as_errors;
    // --thread-safety implicitly enables the discarded-qualifiers warning so
    // _Atomic cast stripping is diagnosed without requiring -Wall.
    if (flags & CCCC_THREAD_SAFETY)
        warnings |= CCCC_WARN_DISCARDED_QUALIFIERS;
    vm.compiler.warnings          = warnings;
    vm.compiler.warning_errors    = warning_errors;
    vm.compiler.warning_no_errors = warning_no_errors;
    jmp_buf err_buf;
    vm.error_jmp_buf = &err_buf;

    // Set up error handling with setjmp/longjmp
    if (setjmp(err_buf) != 0) {
        // Error occurred during compilation
        cc_print_all_errors(&vm);
        exit_code =
            vm.dbg.host_fault_signal ? 128 + vm.dbg.host_fault_signal : 1;
        goto BAIL;
    }

    if (!skip_stdlib)
        cc_load_stdlib(&vm);

    // CCCC's standard library header directory is no longer pushed as an
    // -I entry here: search_include_paths() resolves standard headers from
    // the embedded src/std.c table first, with vm->compiler.builtin_include_dir
    // (set in cc_init) as an on-disk fallback. See man/HEADERS.md.

    // Add user-specified include paths (these take precedence via search order)
    for (int i = 0; i < inc_paths_count; i++)
        cc_include(&vm, inc_paths[i]);

    // Add system include paths (for non-standard headers with angle brackets)
    for (int i = 0; i < sys_inc_paths_count; i++)
        cc_system_include(&vm, sys_inc_paths[i]);

    // --sysroot: auto-configure system include paths from the SDK root.
    // Implies --use-system-headers.
    if (sysroot) {
        struct stat _st;
        char        sysroot_inc[4096];
        snprintf(sysroot_inc, sizeof(sysroot_inc), "%s/usr/include", sysroot);
        if (!stat(sysroot_inc, &_st))
            cc_system_include(&vm, sysroot_inc);
        char sysroot_local_inc[4096];
        snprintf(sysroot_local_inc, sizeof(sysroot_local_inc),
                 "%s/usr/local/include", sysroot);
        if (!stat(sysroot_local_inc, &_st))
            cc_system_include(&vm, sysroot_local_inc);
    }

    // Propagate system-header mode flags to the compiler state.
    vm.compiler.use_system_headers  = use_system_headers;
    vm.compiler.no_builtin_includes = no_builtin_includes;

    for (int i = 0; i < defines_count; i++)
        parse_define(&vm, (char *)defines[i]);
    for (int i = 0; i < undefs_count; i++)
        cc_undef(&vm, (char *)undefs[i]);

    // #888: snapshot the macro table here, right after -D/-U processing and
    // before the primary file is preprocessed. A same-named source #define
    // later overwrites the -D entry in vm.compiler.macros;
    // isolate_comptime_macros then strips it (define_tok != NULL) and the -D
    // value is gone from the comptime pass. This snapshot lets
    // isolate_comptime_macros re-apply any -D that got shadowed that way.
    // macro_snapshot_backup (macros.c) is taken later, after the runtime
    // preprocess, so it is already polluted with source #defines and cannot be
    // reused for this.
    vm.compiler.cli_macro_snapshot     = hashmap_snapshot(&vm.compiler.macros);
    vm.compiler.has_cli_macro_snapshot = true;

    vm.compiler.skip_preprocess        = skip_preprocess;

    // #1002 (investigation): record every command-line input path before
    // preprocessing any of them, so the serializer can ask "was this Obj
    // written in a file the user asked to compile" for *any* input index,
    // not just the first (vm.compiler.primary_file, set by cc_preprocess/
    // linker.c, only ever names input_files[0] -- see
    // file_is_command_line_input in serialize_program.c). Keys are borrowed:
    // these strings are strdup'd once at argv-parsing time and outlive the vm.
    for (int i = 0; i < input_files_count; i++)
        hashmap_put_borrowed(&vm.compiler.command_line_inputs, input_files[i],
                             (void *)(intptr_t)1);

    input_tokens = calloc(input_files_count, sizeof(Token *));
    for (int i = 0; i < input_files_count; i++) {
        // #1001: each command-line input file is its own translation unit,
        // so it gets its own preprocessor state -- a #define, #pragma once,
        // or include guard set while preprocessing file i must not still be
        // in effect when file i+1 is preprocessed. Skipped before the
        // first file (nothing to reset yet).
        if (i > 0)
            cc_reset_preprocessor_state_for_next_tu(&vm);

        // #1007: inject mode headers (testing.h/building.h) into *every*
        // TU's own parse stream, not just input_tokens[0]. Their real
        // payload is a side effect of preprocessing them -- registering the
        // Assert*/build macros into vm.compiler.macros -- and
        // cc_reset_preprocessor_state_for_next_tu() above deliberately
        // undoes that (#1001) ahead of each TU after the first, restoring
        // the macro table to the -D/-U CLI baseline. A one-shot injection
        // done before this loop (the pre-#1007 shape) therefore only ever
        // benefited TU 0: a later TU never saw the Assert* macros expand at
        // all (an "undefined function: AssertEq" from a still-unexpanded
        // macro call, not a missing __builtin_assert_eq prototype), and even
        // if it had, the prototypes themselves were only ever spliced onto
        // input_tokens[0]. When both --testing and --build are active, both
        // headers are injected and their declaration lists are chained
        // together, same as before.
        Token *test_decls = NULL;
        if (testing_mode)
            test_decls = cc_inject_test_header(&vm);
        if (build_mode) {
            Token *build_decls = cc_inject_build_header(&vm);
            if (build_decls && test_decls) {
                Token *tail = build_decls;
                while (tail->next && tail->next->kind != TK_EOF)
                    tail = tail->next;
                tail->next = test_decls;
                test_decls = build_decls;
            } else if (build_decls) {
                test_decls = build_decls;
            }
        }

        input_tokens[i] = cc_preprocess(&vm, input_files[i]);
        if (!input_tokens[i]) {
            fprintf(stderr, "error: failed to preprocess %s\n", input_files[i]);
            goto BAIL;
        }

        // Prepend this TU's injected header declarations onto its own
        // parse stream (mirrors the pre-#1007 input_tokens[0]-only splice,
        // now applied per TU).
        if (test_decls) {
            Token *last = test_decls;
            while (last->next && last->next->kind != TK_EOF)
                last = last->next;
            last->next      = input_tokens[i];
            input_tokens[i] = test_decls;
        }
    }

    // Check for errors and warnings after preprocessing
    if (cc_has_errors(&vm) || vm.warning_count > 0) {
        cc_print_all_errors(&vm);
        if (cc_has_errors(&vm)) {
            exit_code = 1;
            goto BAIL;
        }
        // #686: warnings have been printed; clear them so later checkpoints
        // (which reprint the whole vm->errors list) don't print them again.
        cc_clear_errors(&vm);
    }

    // If -E flag is set, output preprocessed source and exit
    if (preprocess_only) {
        for (int i = 0; i < input_files_count; i++) {
            FILE *f = out_file ? fopen(out_file, "w") : stdout;
            if (!f) {
                fprintf(stderr, "error: failed to open output file %s\n",
                        out_file);
                goto BAIL;
            }

            cc_output_preprocessed(f, &vm, input_tokens[i]);
            if (f != stdout)
                fclose(f);
        }
        goto BAIL;
    }

    // Execute inline (#pragma macro inline) macros before parsing.
    // This compiles and runs them now, synthesises forward declarations for
    // any functions they generate, and prepends those declarations to every
    // input token stream so the parser can resolve calls without manual
    // forward declarations.
    cc_execute_inline_macros(&vm, input_tokens, input_files_count);

    // Register test-runtime FFI symbols after the comptime pass so that any
    // [[cccc::comptime]] calling Assert produces an unresolved-symbol error
    // instead of longjmp-ing through an uninitialised jmp_buf (ticket #334).
    if (testing_mode)
        cc_load_test_runtime(&vm);
    if (build_mode)
        cc_load_build_runtime(&vm);
    cc_load_symbolize_runtime(&vm);

    input_progs = calloc(input_files_count, sizeof(Obj *));
    for (int i = 0; i < input_files_count; i++) {
        // #1001: leave the *previous* TU's file scope before parsing this
        // one, so this TU's declarations can't be resolved through an
        // earlier TU's typedefs/tags/file-scope variables (find_var(),
        // parse.c, walks the whole scope chain). Deliberately not done
        // after every parse (e.g. inside cc_parse() itself): the *last*
        // TU's file scope must stay reachable afterward, since
        // compile_macro_program() (macros.c, run later via
        // cc_expand_macros) relies on it to resolve a `$identifier`
        // reflect-operator reference to a runtime symbol from inside a
        // macro function body -- see cc_leave_top_file_scope's comment
        // (parse.c) for the full reasoning.
        if (i > 0)
            cc_leave_top_file_scope(&vm);
        input_progs[i] = cc_parse(&vm, input_tokens[i]);
        // #999: parse() returning NULL is not on its own a failure -- its
        // own comment says outright it "returns NULL when no new globals
        // were created", which is the ordinary, successful outcome for a
        // TU holding only typedefs/prototypes and no definitions, not just
        // the macro_globals case handled below. Treating it as fatal
        // unconditionally used to print a bogus "failed to parse" message
        // while leaving exit_code at its default 0 (silent success dressed
        // up as a failure) -- and, worse, masked a *genuine* parse error
        // recorded via error_tok_recover (which also leaves globals empty
        // when every top-level declaration in the TU failed to recover
        // into one) behind that same generic message instead of the real
        // diagnostic. Do nothing here in either case: a NULL prog
        // contributes no globals to cc_link_progs (which already tolerates
        // one -- its per-Obj loop over `progs[i]` just doesn't execute,
        // same as if this TU had never been linked at all), and any real
        // error is caught with its actual message and exit_code = 1 by the
        // cc_has_errors() check immediately below this loop.
        if (!input_progs[i] && vm.compiler.macro_globals)
            input_progs[i] = vm.compiler.macro_globals;
    }

    // Check for errors after parsing
    if (cc_has_errors(&vm)) {
        cc_print_all_errors(&vm);
        exit_code = 1;
        goto BAIL;
    }

    // For --ffi-decls, emit parsed function/struct/enum declarations. We
    // don't need to link (especially useful for header files without main()).
    if (output_ffi_decls) {
        // Link programs, but don't fail if linking fails (e.g., no main() in
        // header file)
        Obj *merged_prog = cc_link_progs(&vm, input_progs, input_files_count);
        if (!merged_prog && input_files_count == 1) {
            // If linking failed and we have a single file, just use that file's
            // AST
            merged_prog = input_progs[0];
        } else if (!merged_prog) {
            fprintf(stderr, "error: failed to link programs for JSON output\n");
            goto BAIL;
        }

        FILE *f = out_file ? fopen(out_file, "w") : stdout;
        if (!f) {
            fprintf(stderr, "error: failed to open output file %s\n", out_file);
            goto BAIL;
        }
        cc_output_json(f, merged_prog);
        if (f != stdout)
            fclose(f);
        goto BAIL;
    }

    // Link all programs together
    Obj *merged_prog = cc_link_progs(&vm, input_progs, input_files_count);
    if (!merged_prog) {
        fprintf(stderr, "error: failed to link programs\n");
        goto BAIL;
    }

    // Inject inline-macro-generated function definitions into the program.
    // These Objs were created by __builtin_ast_function during pre-parse inline
    // macro execution and stashed in vm.compiler.macro_globals. They must be
    // in the prog list so cc_compile calls gen_function on them and the
    // call-patcher can resolve calls to them by name.
    // If macro_globals IS the main program (parse() returned it because no
    // new globals were created), don't double-prepend it.
    if (vm.compiler.macro_globals && merged_prog != vm.compiler.macro_globals) {
        Obj *tail = vm.compiler.macro_globals;
        while (tail->next)
            tail = tail->next;
        tail->next  = merged_prog;
        merged_prog = vm.compiler.macro_globals;
    }

    // Expand macros in the AST
    cc_expand_macros(&vm, merged_prog);

    // Check for errors and warnings after macro expansion (ticket #78)
    if (cc_has_errors(&vm) || vm.warning_count > 0) {
        cc_print_all_errors(&vm);
        if (cc_has_errors(&vm)) {
            exit_code = 1;
            goto BAIL;
        }
        // #686: warnings have been printed; clear them so later checkpoints
        // (which reprint the whole vm->errors list) don't print them again.
        cc_clear_errors(&vm);
    }

    // If -m/--dump-expanded (or -c=generated) is set, output
    // macro-expanded/serialized source and exit.
    if (dump_expanded_only) {
        // -c=generated defaults its output file the same way -c=native
        // does (a.out), unlike plain -m which still falls back to stdout
        // when -o is omitted (#936).
        if (compile_format == COMPILE_GENERATED && !out_file)
            out_file = strdup("a.gen.c");
        FILE *f = out_file ? fopen(out_file, "w") : stdout;
        if (!f) {
            fprintf(stderr, "error: failed to open output file %s\n", out_file);
            goto BAIL;
        }
        // #924: checked here (post-preprocess) rather than in the CLI-only
        // validation block above so a flag set via `#pragma cccc
        // config(...)` -- not just the CLI spelling -- is caught too;
        // vm.flags reflects both by this point. Warn, don't error: the
        // attributes these flags gate are already unconditionally stripped
        // from serialized output (#482/#488 ABI transparency), so the flag
        // is genuinely a no-op here, not a conflict.
        vm.flags = warn_ignored_vm_flags(
            vm.flags, emit_generated_only ? "-c=generated" : "-m");
        // #999: this path (-m / -c=generated) bails out before cc_compile()
        // ever runs, so vm.compiler.globals still holds whatever the *last*
        // TU's own parse() call left it as (each TU's parse() resets it to
        // that TU's own list -- see the #957 comment in parse()), not the
        // full merged, multi-TU program. serialize_find_global()
        // (src/serialize_decl.c) scans this list to resolve a Relocation's
        // target (e.g. a function pointer in a static const vtable
        // initializer, `.open = none_open`), so a target defined in an
        // earlier TU than the one holding the initializer was reported as
        // an unresolved relocation even though it's really just not merged
        // in yet. Mirrors bytecode.c's own `vm->compiler.globals = prog`
        // assignment (the -c=native path, which runs cc_compile() first
        // and already sees the merged list) for the same reason.
        vm.compiler.globals = merged_prog;
        cc_serialize_program(f, &vm, merged_prog, emit_generated_only, false);
        // #1017: as above (run_native_backend) -- a warning queued by
        // cc_serialize_program() itself (e.g. CCCC_WARN_NATIVE_NAME_COLLISION)
        // is otherwise silently dropped, since this path bails out to BAIL
        // right after without ever reaching the later cc_has_errors()/
        // warning_count checkpoints. BAIL itself never reprints vm.errors
        // (cc_destroy() below it just frees the parser arena vm.errors
        // lives in), so the clear here is for symmetry with every other
        // checkpoint's print+clear pairing, not strictly required.
        if (cc_has_errors(&vm) || vm.warning_count > 0) {
            cc_print_all_errors(&vm);
            cc_clear_errors(&vm);
        }
        if (f != stdout) {
            fclose(f);
            if (compile_format == COMPILE_GENERATED)
                fprintf(stderr, "Generated C written to %s\n", out_file);
        }
        goto BAIL;
    }

    // Merge libraries requested via #pragma cccc link(...) into the
    // -l/--library list (#357), so they get FFI-resolved (and, for -c=native,
    // linked) the same as -l. Copies are made so `libs[]` and
    // vm.compiler.pragma_link_libs each own their own strings (both are freed
    // independently at cleanup).
    for (int i = 0; i < vm.compiler.pragma_link_libs.len; i++) {
        libs               = realloc(libs, sizeof(*libs) * (libs_count + 1));
        libs[libs_count++] = strdup(vm.compiler.pragma_link_libs.data[i]);
    }

    if (libs_count > 0)
        register_dynamic_externs(&vm, merged_prog);

    if (print_tokens) {
        for (int i = 0; i < input_files_count; i++) {
            printf("=== Tokens for %s ===\n", input_files[i]);
            cc_print_tokens(input_tokens[i]);
            printf("\n");
        }
        goto BAIL;
    }

    if (dump_ast) {
        FILE *f = out_file ? fopen(out_file, "w") : stdout;
        if (!f) {
            fprintf(stderr, "error: failed to open output file %s\n", out_file);
            exit_code = 1;
            goto BAIL;
        }
        if (output_json)
            cc_dump_ast_json(f, merged_prog, verbose);
        else
            cc_dump_ast(f, merged_prog, verbose);
        if (f != stdout)
            fclose(f);
        goto BAIL;
    }

    // Compile the merged program
    cc_compile(&vm, merged_prog);

    // Check for errors after code generation
    if (cc_has_errors(&vm)) {
        cc_print_all_errors(&vm);
        exit_code = 1;
        goto BAIL;
    }

    // --test-run: smoke-test the compiled program in the VM before
    // proceeding to the compile step below. Runs in a forked child so the
    // parent's vm (text/data segments) and merged_prog stay pristine for
    // the compile step that follows -- cc_run() below mutates guest globals
    // in place, so running in-process here would silently bake the test
    // run's post-execution global values into the program's real initial
    // state. The native path doesn't share this hazard (run_native_backend
    // re-derives C from the untouched AST), but forking unconditionally
    // keeps one code path for both.
    if (test_run_mode) {
#if defined(_WIN32)
        fprintf(stderr,
                "error: --test-run is not supported on this platform\n");
        exit_code = 1;
        goto BAIL;
#else
        int    prog_argc = 0;
        char **prog_argv =
            build_source_argv(&prog_argc, argc, argv, optind, dashdash);
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "error: --test-run: fork failed: %s\n",
                    strerror(errno));
            exit_code = 1;
            goto BAIL;
        }
        if (pid == 0) {
            // Child: cap the smoke test so a hang can't wedge the build
            // (reuses --test-timeout's default when the user didn't set
            // one explicitly).
            alarm(test_timeout > 0 ? (unsigned)test_timeout : 30);
            int rc = cc_run(&vm, prog_argc, prog_argv);
            // _exit() skips stdio flushing (unlike exit()/a normal return
            // from main, which is how every other cc_run() call site in
            // this file gets its diagnostics out) -- without this, a VM
            // safety violation's printf() diagnostic (src/ops.c) is silently
            // dropped. Pass rc through as-is: the OS truncates it to the low
            // 8 bits on the way out, the same -1-becomes-255 conversion the
            // normal `return exit_code;` path at the bottom of main() gets
            // for free, so the 255 check below sees it identically.
            fflush(NULL);
            _exit(rc);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        bool test_run_ok;
        if (WIFSIGNALED(status)) {
            fprintf(stderr,
                    "error: --test-run: program terminated by signal %d "
                    "(%s) under the VM smoke test -- refusing to compile\n",
                    WTERMSIG(status), strsignal(WTERMSIG(status)));
            test_run_ok = false;
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 255) {
            // 255 is CCCC's own runtime/safety-error exit convention (see
            // man/TESTING.md's EXPECT_RUNTIME_ERROR), delivered as a normal
            // process exit rather than a host signal -- a VM-detected
            // safety violation (bounds/UAF/CFI/uninit/etc.) reaches here,
            // not WIFSIGNALED above, so it needs its own check.
            fprintf(stderr,
                    "error: --test-run: safety violation detected under the "
                    "VM smoke test -- refusing to compile\n");
            test_run_ok = false;
        } else {
            // Otherwise success = "ran to completion without a crash/safety
            // violation/hang" -- any other exit code is deliberately not
            // checked, since a program that legitimately returns nonzero
            // (e.g. a CLI reporting bad input) is not a --test-run failure.
            test_run_ok = WIFEXITED(status);
            if (!test_run_ok)
                fprintf(stderr,
                        "error: --test-run: program did not exit cleanly "
                        "under the VM smoke test -- refusing to compile\n");
        }
        free(prog_argv);
        if (!test_run_ok) {
            exit_code = 1;
            goto BAIL;
        }
#endif
    }

    // -c/--compile: hand off to native (or serialize generated C) and exit.
    // This runs BEFORE the legacy bare "-o with no -c" error branch below so
    // -c=native can short-circuit out of the VM runtime path. The order is
    // also the fix for ticket #300: previously `compile_only` short-circuited
    // at `goto BAIL;` before that branch, silently swallowing `-c -o <file>`.
    // --testing=native skips cc_run_tests entirely -- there's no in-process
    // harness to run against; control falls through to the
    // compile_format == COMPILE_NATIVE dispatch below, which serializes the
    // [[cccc::test]] harness itself into the generated C (#1033).
    // The vm backend runs the suite as a pre-pass (#1106): on a passing (or
    // --list-tests) run, control falls through into the -c dispatches below,
    // so `--testing -c=native` really does compile after the tests; a
    // failing suite bails before any artifact is written.
    if (testing_mode && testing_backend != TESTING_BACKEND_NATIVE) {
        CcTestOptions test_opts = {
            .test_glob    = test_glob,
            .suite_filter = suite_filter,
            .list_only    = (bool)list_tests,
            .fail_fast    = (bool)fail_fast,
            .test_timeout = test_timeout,
            .format       = test_format,
        };

        exit_code = cc_run_tests(&vm, merged_prog, &test_opts);

        // Fall through into the compile dispatch below only when there is a
        // compile step to run AND the suite passed (#1106): --testing acts as
        // a pre-pass guard, so -c=native/--build both refuse to produce an
        // artifact once any test has failed. This is deliberately independent
        // of --fail-fast, which only stops the test *run* early; a red suite
        // never reaches the compile step.
        if ((!build_mode && compile_format != COMPILE_NATIVE) ||
            exit_code != 0)
            goto BAIL;
    }

    if (build_mode) {
        // A build script must not define main() in --build mode.
        for (Obj *o = merged_prog; o; o = o->next) {
            if (o->is_function && o->name && strcmp(o->name, "main") == 0) {
                fprintf(stderr,
                        "error: a --build script must not define main()\n");
                exit_code = 1;
                goto BAIL;
            }
        }

        CcNativeCompileArgs build_defaults = {
            .inc_paths           = inc_paths,
            .inc_paths_count     = inc_paths_count,
            .sys_inc_paths       = sys_inc_paths,
            .sys_inc_paths_count = sys_inc_paths_count,
            .lib_paths           = lib_paths,
            .lib_paths_count     = lib_paths_count,
            .libs                = libs,
            .libs_count          = libs_count,
            .defines             = defines,
            .defines_count       = defines_count,
            .undefs              = undefs,
            .undefs_count        = undefs_count,
            .std_arg             = std_arg,
        };
        CcBuildOptions build_opts = {
            .entry_name          = build_entry,
            .target_name         = build_target,
            .out_dir             = build_out_dir,
            .verbose             = verbose,
            .build_verbose       = build_verbose,
            .quiet               = build_quiet,
            .keep_going          = build_keep_going,
            .dry_run             = build_dry_run,
            .jobs                = build_jobs,
            .defaults            = &build_defaults,
            .tool_allow          = build_tool_allow,
            .tool_allow_count    = build_tool_allow_count,
            .list_targets        = build_list_targets,
            .profile             = build_profile,
            .cross_triple        = build_triple,
            .cross_cc            = build_cc,
            .build_cache         = build_cache,
            .build_options       = build_options,
            .build_options_count = build_options_count,
            .build_install       = build_install,
            .user_args           = (dashdash >= 0 && dashdash + 1 < argc)
                                       ? argv + dashdash + 1
                                       : NULL,
            .user_args_count     = (dashdash >= 0 && dashdash + 1 < argc)
                                       ? argc - dashdash - 1
                                       : 0,
        };

        int build_code = cc_run_build(&vm, merged_prog, &build_opts);
        if (build_code != 0)
            exit_code = build_code;

        goto BAIL;
    }

    if (compile_format == COMPILE_NATIVE) {
        if (testing_backend == TESTING_BACKEND_NATIVE) {
            // The generated harness supplies its own main(); a test file
            // that defines one too would collide (mirrors the --build
            // check above).
            for (Obj *o = merged_prog; o; o = o->next) {
                if (o->is_function && o->name && strcmp(o->name, "main") == 0) {
                    fprintf(stderr,
                            "error: --testing=native: input must not define "
                            "main() -- the generated harness supplies its "
                            "own\n");
                    exit_code = 1;
                    goto BAIL;
                }
            }
            // #1033 v1 scope cuts, refused with a clear diagnostic rather
            // than silently mis-serialized: [[cccc::test_setup/teardown]]
            // hooks have no fork-safe native equivalent (a `once` hook
            // exists precisely so a mutation outlives one test, which
            // can't work once each test is its own forked child), and a
            // negative test's ([[cccc::test(error=...)]] /
            // expect_compile_error=) body is the parser's error-recovery
            // AST for source that was never meant to compile cleanly --
            // not something safe to hand to a real host compiler. Both are
            // narrow in practice (7 and 4 files respectively in this
            // repo's own suite corpus) and stay on --testing=vm.
            if (vm.compiler.test_setups) {
                fprintf(stderr,
                        "error: --testing=native: [[cccc::test_setup]] / "
                        "[[cccc::test_teardown]] hooks are not supported "
                        "(#1033 v1) -- use --testing=vm\n");
                exit_code = 1;
                goto BAIL;
            }
            for (TestFnRecord *r = vm.compiler.test_fns; r; r = r->next) {
                if (r->error_pat || r->expect_compile_error) {
                    fprintf(stderr,
                            "error: --testing=native: negative test '%s' "
                            "(error=/expect_compile_error=) is not "
                            "supported (#1033 v1) -- use --testing=vm\n",
                            r->display_name ? r->display_name : r->name);
                    exit_code = 1;
                    goto BAIL;
                }
            }
            if (!vm.compiler.test_fns) {
                fprintf(stderr, "error: --testing=native: no [[cccc::test]] "
                                "functions found in any input file\n");
                exit_code = 1;
                goto BAIL;
            }
        }
        // -c=native defaults out_file to "a.out" above, so exe_path is
        // always a non-NULL path here.
        exit_code = run_native_backend(
            &vm, merged_prog, out_file, inc_paths, inc_paths_count,
            sys_inc_paths, sys_inc_paths_count, lib_paths, lib_paths_count,
            libs, libs_count, defines, defines_count, undefs, undefs_count,
            std_arg, (bool)emit_cccc_mode,
            testing_backend == TESTING_BACKEND_NATIVE, opt_level);
        goto BAIL;
    }

    if (load_requested_libraries(&vm, libs, libs_count, lib_paths,
                                 lib_paths_count) != 0) {
        exit_code = 1;
        goto BAIL;
    }
    if (verify_dynamic_externs(&vm) != 0) {
        exit_code = 1;
        goto BAIL;
    }

    if (disassemble) {
        cc_disassemble(&vm);
        goto BAIL;
    }

    // Static bytecode analysis on the just-compiled text segment.
    if (run_ngrams || run_fusion) {
        if (run_ngrams) {
            CcAnalyzeNgramOptions opts = {
                .n        = run_ngrams,
                .top_n    = ngrams_top,
                .per_file = ngrams_per_file,
            };
            ngram_state = cc_analyze_ngram_begin(&opts);
            const char *label =
                input_files_count == 1 ? input_files[0] : "<merged source>";
            cc_analyze_ngram_feed(ngram_state, vm.text_seg,
                                  (long long)vm.text_ptr + 1, label, stdout);
            cc_analyze_ngram_finish(ngram_state, stdout);
            ngram_state = NULL;
        } else {
            CcAnalyzeFusionOptions opts = {
                .top_n = run_fusion,
                .json  = output_json,
            };
            fusion_state = cc_analyze_fusion_begin(&opts);
            const char *label =
                input_files_count == 1 ? input_files[0] : "<merged source>";
            cc_analyze_fusion_feed(fusion_state, vm.text_seg,
                                   (long long)vm.text_ptr + 1, label, stdout);
            cc_analyze_fusion_finish(fusion_state, stdout);
            fusion_state = NULL;
        }
        goto BAIL;
    }

    if (out_file) {
        // -o with no -c/--compile format has no artifact left to write --
        // on-disk bytecode output was removed. Use -c=native (or
        // -c=generated) to produce a file.
        fprintf(stderr,
                "error: -o requires -c/--compile (native or generated); "
                "plain -o with no compile format has nothing to write\n");
        exit_code = 1;
        goto BAIL;
    }

    // Run the program. If "--" was given, forward only args after it; otherwise
    // fall back to the old behaviour of passing all positional args.
    int    prog_argc = 0;
    char **prog_argv =
        build_source_argv(&prog_argc, argc, argv, optind, dashdash);
    exit_code        = cc_run(&vm, prog_argc, prog_argv);
    vm_profile_mode  = "source";
    vm_profile_input = input_files_count == 1 ? input_files[0] : "multiple";
    vm_profile_ran   = 1;
    free(prog_argv);

BAIL:
    if (vm_profile && vm_profile_ran) {
        if (vm_profile_text)
            cc_vm_profile_print(&vm, stderr);
        if (output_json &&
            cc_vm_profile_write_json(&vm, stdout, vm_profile_mode,
                                     vm_profile_input) != 0) {
            fprintf(stderr,
                    "error: failed to write VM profile JSON to stdout\n");
            if (exit_code == 0 || exit_code == 42)
                exit_code = 1;
        }
    }
    cc_destroy(&vm);
    // Defensive: free analysis state if it was allocated but not finalized
    // (shouldn't happen given the dispatch flow, but keeps leak-checkers
    // happy).
    if (ngram_state) {
        cc_analyze_ngram_finish(ngram_state, stdout);
    }
    if (fusion_state) {
        cc_analyze_fusion_finish(fusion_state, stdout);
    }
    if (input_tokens)
        free(input_tokens);
    if (input_progs) {
        // Don't free individual Obj* - they're arena-allocated and freed by
        // cc_destroy()
        free(input_progs);
    }
    if (out_file)
        free(out_file);
    if (inc_paths) {
        for (int i = 0; i < inc_paths_count; i++)
            free((void *)inc_paths[i]);
        free(inc_paths);
    }
    if (lib_paths) {
        for (int i = 0; i < lib_paths_count; i++)
            free((void *)lib_paths[i]);
        free(lib_paths);
    }
    if (libs) {
        for (int i = 0; i < libs_count; i++)
            free((void *)libs[i]);
        free(libs);
    }
    if (ffi_allow_args) {
        for (int i = 0; i < ffi_allow_args_count; i++)
            free((void *)ffi_allow_args[i]);
        free(ffi_allow_args);
    }
    if (ffi_deny_args) {
        for (int i = 0; i < ffi_deny_args_count; i++)
            free((void *)ffi_deny_args[i]);
        free(ffi_deny_args);
    }
    if (sys_inc_paths) {
        for (int i = 0; i < sys_inc_paths_count; i++)
            free((void *)sys_inc_paths[i]);
        free(sys_inc_paths);
    }
    if (defines) {
        for (int i = 0; i < defines_count; i++)
            free((void *)defines[i]);
        free(defines);
    }
    if (undefs) {
        for (int i = 0; i < undefs_count; i++)
            free((void *)undefs[i]);
        free(undefs);
    }
    if (input_files) {
        for (int i = 0; i < input_files_count; i++)
            free((void *)input_files[i]);
        free(input_files);
    }
    free(build_options);
    if (build_tool_allow) {
        for (int i = 0; i < build_tool_allow_count; i++)
            free((void *)build_tool_allow[i]);
        free(build_tool_allow);
    }
    return exit_code;
}
