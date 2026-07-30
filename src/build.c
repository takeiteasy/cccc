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

// --build mode runtime.  Mirrors src/testing.c: it injects a private header
// (building.h), registers the builder API as FFI-callable cfuncs, then finds
// and invokes the build entry in the VM.  The entry only *declares* a target
// graph (pure data); the host-side runner in this file compiles and links it
// with the system toolchain (cc/ar/ld), synchronously, inside the
// cccc_build_run* FFI call so the entry's return value reflects the real status.

#include "./internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _POSIX_VERSION
#include <unistd.h>
#include <fnmatch.h>
#include <glob.h>
#include <sys/wait.h>
#include <dirent.h>
#endif

#include "paul_shell.h"

#if defined(__APPLE__)
#define CCCC_BUILD_HOST "darwin"
#define CCCC_DYLIB_EXT  "dylib"
#elif defined(__linux__)
#define CCCC_BUILD_HOST "linux"
#define CCCC_DYLIB_EXT  "so"
#elif defined(_WIN32)
#define CCCC_BUILD_HOST "windows"
#define CCCC_DYLIB_EXT  "dll"
#else
#define CCCC_BUILD_HOST "unknown"
#define CCCC_DYLIB_EXT  "so"
#endif

// ============================================================================
// Target graph storage
// ============================================================================

typedef enum {
    CCCC_TGT_EXE,
    CCCC_TGT_STATIC,
    CCCC_TGT_DYNAMIC,
    CCCC_TGT_CUSTOM,   // arbitrary shell command (#544)
    CCCC_TGT_BYTECODE, // whole-program cccc compile → .c4 executable (#545)
} CcTargetKind;

typedef struct BuildTarget BuildTarget;
typedef struct Builder Builder;

struct BuildTarget {
    CcTargetKind kind;
    char *name;
    char *output;          // explicit output path (relative to out_dir) or NULL
    char *command;         // CCCC_TGT_CUSTOM: shell command to run
    char *profile;         // "debug"|"release"|"relwithdebinfo"|"minsizerel", or NULL (#548)
    char *cc_override;     // per-target compiler binary; NULL = use ctx->cross_cc or global (#547)
    char *target_triple;   // per-target cross-compilation triple; NULL = inherit from ctx (#547)
    StringArray sources;
    StringArray excludes;  // glob/path patterns excluded from sources (#542)
    StringArray includes;
    StringArray defines;   // "NAME=VALUE" or "NAME"
    StringArray undefs;
    StringArray cflags;
    StringArray ldflags;
    StringArray libs;      // bare -l names
    StringArray libpaths;  // -L paths
    StringArray env;       // "NAME=VALUE" environment overrides for the
                            // target's compiler/linker child (#842, SetTargetEnv)
    BuildTarget **deps;
    int *deps_link;        // parallel to deps: 1=LinkWith (add -l), 0=DependsOn (order only)
    int deps_count, deps_cap;
    int visited;           // topo-sort marker: 0 unvisited, 1 in-progress, 2 done
    int state;             // parallel dispatch state: see TARGET_* constants (#557)
    // Set by run_graph() for targets that are LinkWith'd (transitively) from a
    // kind=bytecode target (#563).  Such a target is a "bytecode library": its
    // sources are folded into the dependent's single cccc invocation and it is
    // not built standalone.
    int bytecode_folded;
    // Subkind for CCCC_TGT_BYTECODE targets (#564): 0=exe, 1=static lib, 2=dynamic lib.
    // Static libs (.c4a) are built standalone; bytecode EXEs link them via --link (#565).
    // Dynamic libs (.c4d) are runtime-loaded and never folded into anything.
    int bytecode_subkind;
};

// Parallel dispatch states for BuildTarget.state (#557).
#define TARGET_PENDING  0  // not yet started
#define TARGET_INFLIGHT 1  // forked child running
#define TARGET_DONE     2  // built successfully
#define TARGET_FAILED   3  // build failed
#define TARGET_SKIPPED  4  // blocked: a dep failed/was skipped

struct Builder {
    char *root;
    char *out_dir;
    const char *host;
    const char *target_filter; // --build-target=NAME, or NULL (build all)
    int verbose;           // -v or --build-verbose: enables per-target headers
    int quiet;             // --build-quiet: suppress per-step command lines
    int keep_going;        // --build-keep-going: continue past target failures
    int dry_run;
    int jobs;              // --build-jobs=N: max parallel cc -c slots (<=1 = serial)
    char *cache_dir;       // --build-cache[=PATH]: content-addressable cache root, or NULL (#546)
    BuildTarget **targets;
    int targets_count, targets_cap;
    const CcNativeCompileArgs *defaults; // CLI -I/-D/-U/--std/-l/-L forwarded
    int run_invoked;       // set once a cccc_build_run* is called
    int failed;            // non-zero once any build step fails
    // Tool gating (#543): if tool_allow_count > 0, only listed tools may run
    // via RunCustom / HaveTool / PkgConfig.  cc/ar/ld bypass this check (they
    // go through run_argv, not the shell).
    const char **tool_allow;
    int tool_allow_count;
    // [[cccc::build_target]] factory names (#540): copied from compiler state
    // before the entry runs; used by --build-list-targets and factory-direct
    // --build-target=NAME invocation.
    const char **factory_names;
    int factory_count;
    const char *profile;      // --build-profile=NAME global default (#548)
    const char *cross_triple; // --build-triple=TRIPLE global target triple (#547)
    const char *cross_cc;     // --build-cc=COMPILER global CC override (#547)
    const char *cccc_self;    // path to the cccc binary; used for kind=bytecode targets (#545)
    // Strings returned by CaptureCommand/GlobFiles/ReadFile; freed at teardown.
    char **captures;
    int captures_count, captures_cap;
    // Build options from --build-option=key=value CLI flags (#559)
    const char **build_options;
    int build_options_count;
    // Install support (#560)
    char         *install_prefix;  // default: PREFIX env or /usr/local
    BuildTarget **install_targets;
    int           install_count, install_cap;
    int           build_install;   // --build-install flag
    // User args forwarded from -- on the CLI (#558)
    const char **user_args;
    int          user_args_count;
    // CWD saved on first SetCwd call; restored at teardown (#569)
    char *original_cwd;
};

// The active build context for the current --build run.  Set by cc_run_build
// before invoking the entry and cleared afterward.  The builder API ignores its
// cosmetic `ctx` handle (a 64-bit pointer cannot be threaded through cc_run_at's
// int argc slot) and uses this singleton instead -- one build run, one context.
static Builder *s_ctx = NULL;

// ============================================================================
// Small helpers
// ============================================================================

static char *xstrdup(const char *s) {
    char *p = strdup(s ? s : "");
    if (!p)
        error("build: out of memory");
    return p;
}

// mkdir -p semantics for a directory path.
static int mkdir_p(const char *path) {
    if (!path || !*path)
        return 0;
    char *tmp = xstrdup(path);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/')
        tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    int rc = (mkdir(tmp, 0755) != 0 && errno != EEXIST) ? -1 : 0;
    free(tmp);
    return rc;
}

// Directory portion of `path`, into a freshly allocated string ("." if none).
static char *dir_of(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash)
        return xstrdup(".");
    size_t n = (size_t)(slash - path);
    char *d = malloc(n + 1);
    if (!d)
        error("build: out of memory");
    memcpy(d, path, n);
    d[n] = '\0';
    return d;
}

// Base name without directory or extension (for object file naming).
static char *stem_of(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t n = dot ? (size_t)(dot - base) : strlen(base);
    char *s = malloc(n + 1);
    if (!s)
        error("build: out of memory");
    memcpy(s, base, n);
    s[n] = '\0';
    return s;
}

// Join out_dir + "/" + rel into a freshly allocated path.
static char *join(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b);
    char *r = malloc(na + nb + 2);
    if (!r)
        error("build: out of memory");
    snprintf(r, na + nb + 2, "%s/%s", a, b);
    return r;
}

// Forward declaration: defined with the other environment/filesystem
// helpers below; needed early by TargetOutput() (#842).
static const char *builder_intern(Builder *ctx, char *s);

// Default output path (relative to out_dir) for a target.
static char *default_output(const BuildTarget *t) {
    char buf[512];
    switch (t->kind) {
    case CCCC_TGT_EXE:
        snprintf(buf, sizeof(buf), "bin/%s", t->name);
        break;
    case CCCC_TGT_STATIC:
        snprintf(buf, sizeof(buf), "lib/lib%s.a", t->name);
        break;
    case CCCC_TGT_DYNAMIC:
        snprintf(buf, sizeof(buf), "lib/lib%s.%s", t->name, CCCC_DYLIB_EXT);
        break;
    case CCCC_TGT_CUSTOM:
        return xstrdup(""); // custom targets have no output artifact
    case CCCC_TGT_BYTECODE:
        if (t->bytecode_subkind == 1)
            snprintf(buf, sizeof(buf), "lib/%s.c4a", t->name);
        else if (t->bytecode_subkind == 2)
            snprintf(buf, sizeof(buf), "lib/%s.c4d", t->name);
        else
            snprintf(buf, sizeof(buf), "bin/%s.c4", t->name);
        break;
    }
    return xstrdup(buf);
}

// Resolve a target's on-disk output path exactly the way build_target() does
// internally: explicit SetOutput() path if given, else the kind-appropriate
// default (bin/<name>, lib/lib<name>.a, ...). Both paths funnel through the
// same default_output()/join() primitives, so this and build_target()'s own
// computation cannot drift. Returns a freshly allocated absolute path;
// CCCC_TGT_CUSTOM targets have no output artifact (default_output() returns
// "" for them) so this returns "<out_dir>/" for those unless SetOutput() was
// called explicitly — TargetOutput() below special-cases that to plain "".
static char *resolved_target_output(Builder *ctx, const BuildTarget *t) {
    char *out_rel = t->output ? xstrdup(t->output) : default_output(t);
    char *out_abs = join(ctx->out_dir, out_rel);
    free(out_rel);
    return out_abs;
}

// ============================================================================
// Shell passthrough callbacks for RunCustom (#568)
// ============================================================================

// Passthrough stdout/stderr for posix_shell_with_io.  Both callbacks must be
// set; if either is NULL the parent drain loop uses xrealloc/xmalloc instead.
static void build_passthru_out(const char *d, size_t n, void *u) {
    (void)u; fwrite(d, 1, n, stdout); fflush(stdout);
}
static void build_passthru_err(const char *d, size_t n, void *u) {
    (void)u; fwrite(d, 1, n, stderr);
}

// ============================================================================
// Tool gating (#543)
// ============================================================================

// Returns 1 if `name` may be executed by RunCustom / HaveTool / PkgConfig.
// When the allowlist is empty (default), all tools are allowed.
static int tool_allowed(const Builder *ctx, const char *name) {
    if (!name || !*name)
        return 0;
    if (ctx->tool_allow_count == 0)
        return 1; // allow-all (default; preserves v1 behaviour)
    for (int i = 0; i < ctx->tool_allow_count; i++)
        if (strcmp(ctx->tool_allow[i], name) == 0)
            return 1;
    return 0;
}

// ============================================================================
// Capturing subprocess spawn (used by PkgConfig)
// ============================================================================

#ifdef _POSIX_VERSION
// Run argv and capture stdout into *out (heap, NUL-terminated).
// Returns exit code (0 = success, -1 = fork/exec error).
static int run_capture(char *const argv[], char **out) {
    if (!argv || !argv[0]) return -1;
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        // Suppress stderr (pkg-config error messages go there)
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);

    size_t cap = 256, len = 0;
    char *buf = malloc(cap + 1);
    if (!buf) { close(pipefd[0]); waitpid(pid, NULL, 0); return -1; }
    ssize_t n;
    char tmp[4096];
    while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
        if (len + (size_t)n >= cap) {
            cap = (cap + (size_t)n) * 2;
            char *nb = realloc(buf, cap + 1);
            if (!nb) { free(buf); close(pipefd[0]); waitpid(pid, NULL, 0); return -1; }
            buf = nb;
        }
        memcpy(buf + len, tmp, n);
        len += n;
    }
    close(pipefd[0]);
    buf[len] = '\0';
    *out = buf;

    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif

// ============================================================================
// Builder API — FFI-callable.  Pointers are marshalled as int64; the cosmetic
// ctx handle is ignored in favour of s_ctx.
// ============================================================================

// Target names must be unique (#842): two targets sharing a name silently
// share the same build/obj/<name> objdir and build/<default-output> path
// (find_target_by_name() also just returns the first match), which is
// exactly the failure mode a two-pass build (pass1/pass2 compiling the same
// binary name against different inputs) must not hit silently.
static void check_name_unique(Builder *ctx, CcTargetKind kind, const char *name) {
    for (int i = 0; i < ctx->targets_count; i++) {
        if (strcmp(ctx->targets[i]->name, name) == 0)
            error("build: duplicate target name '%s' (first declared as kind %d, "
                  "redeclared as kind %d) — target names must be unique",
                  name, ctx->targets[i]->kind, kind);
    }
}

static BuildTarget *new_target(CcTargetKind kind, const char *name) {
    Builder *ctx = s_ctx;
    if (!ctx)
        error("build: target factory called outside a build run");
    check_name_unique(ctx, kind, name);
    BuildTarget *t = calloc(1, sizeof(*t));
    if (!t)
        error("build: out of memory");
    t->kind = kind;
    t->name = xstrdup(name);
    if (ctx->targets_count >= ctx->targets_cap) {
        ctx->targets_cap = ctx->targets_cap ? ctx->targets_cap * 2 : 8;
        ctx->targets = realloc(ctx->targets,
                               sizeof(*ctx->targets) * ctx->targets_cap);
        if (!ctx->targets)
            error("build: out of memory");
    }
    ctx->targets[ctx->targets_count++] = t;
    return t;
}

static long long impl_executable(long long ctx, long long name) {
    (void)ctx;
    return (long long)(intptr_t)new_target(CCCC_TGT_EXE, (const char *)name);
}
static long long impl_static_lib(long long ctx, long long name) {
    (void)ctx;
    return (long long)(intptr_t)new_target(CCCC_TGT_STATIC, (const char *)name);
}
static long long impl_dynamic_lib(long long ctx, long long name) {
    (void)ctx;
    return (long long)(intptr_t)new_target(CCCC_TGT_DYNAMIC, (const char *)name);
}

static long long impl_set_output(long long t, long long path) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    free(tgt->output);
    tgt->output = xstrdup((const char *)path);
    return 0;
}

// TargetOutput: the on-disk path build_target() will produce for this
// target, so a RunCustom command can reference the binary a dependency just
// built instead of hardcoding a path (#842).
//
// For EXE/STATIC/DYNAMIC/BYTECODE targets this is always <out_dir>/<path> —
// the explicit SetOutput() path if given, else the kind-appropriate default
// (bin/<name>, lib/lib<name>.a, ...) — since those targets' outputs are
// always written under the build output directory.
//
// For CCCC_TGT_CUSTOM targets there is no such convention: a RunCustom
// command can write anywhere (e.g. the stdlib regen writes to the repo's
// src/std.c, not under out_dir). So a CUSTOM target's output is whatever
// DeclareOutput() recorded, returned verbatim (not joined onto out_dir); ""
// if DeclareOutput() was never called.
static long long impl_target_output(long long t) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    if (!s_ctx)
        error("build: TargetOutput called outside a build run");
    if (tgt->kind == CCCC_TGT_CUSTOM)
        return (long long)(intptr_t)builder_intern(s_ctx, xstrdup(tgt->output ? tgt->output : ""));
    char *path = resolved_target_output(s_ctx, tgt);
    return (long long)(intptr_t)builder_intern(s_ctx, path);
}

// DeclareOutput: record the path a CCCC_TGT_CUSTOM step produces — taken
// verbatim (see TargetOutput() above; not joined onto out_dir) — so
// downstream DependsOn consumers are known to depend on that file (#842).
// This is invalidation metadata only — it does NOT give the step a "skip if
// output exists" staleness check.  A CUSTOM target has no `sources`, so
// "output exists" would be the only test available, and for the stdlib regen
// specifically the output (src/std.c) always exists once committed; treating
// that as "up to date" would silently stop the regen from ever running again.
// Real skip semantics need declared *inputs*, not just an output path — a
// follow-up (see #842 Step 6 tickets).
static long long impl_declare_output(long long t, long long path) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    if (tgt->kind != CCCC_TGT_CUSTOM) {
        fprintf(stderr, "build: DeclareOutput is only valid on a RunCustom target ('%s' is not one)\n",
                tgt->name);
        return 0;
    }
    free(tgt->output);
    tgt->output = xstrdup((const char *)path);
    return 0;
}

// SetTargetEnv: set an environment variable ("NAME=VALUE") for this target's
// compiler/linker child process only (#842) — e.g. AFL_USE_ASAN=1 for an
// afl-asan target. Threaded through run_step()/run_argv_env(); has no effect
// on CCCC_TGT_CUSTOM targets, whose RunCustom command runs through the
// vendored shell (build_shell.c), not run_argv.
static long long impl_set_target_env(long long t, long long name, long long value) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    const char *n = (const char *)name;
    const char *v = (const char *)value;
    if (!n || !*n) return 0;
    size_t len = strlen(n) + strlen(v ? v : "") + 2;
    char *entry = malloc(len);
    if (!entry)
        error("build: out of memory");
    snprintf(entry, len, "%s=%s", n, v ? v : "");
    strarray_push(&tgt->env, entry);
    return 0;
}
static long long impl_add_source(long long t, long long path) {
    strarray_push(&((BuildTarget *)(intptr_t)t)->sources, xstrdup((const char *)path));
    return 0;
}
static long long impl_add_include(long long t, long long path) {
    strarray_push(&((BuildTarget *)(intptr_t)t)->includes, xstrdup((const char *)path));
    return 0;
}
static long long impl_add_define(long long t, long long name, long long value) {
    const char *n = (const char *)name;
    const char *v = (const char *)value;
    char *def;
    if (v && *v) {
        size_t len = strlen(n) + strlen(v) + 2;
        def = malloc(len);
        if (!def)
            error("build: out of memory");
        snprintf(def, len, "%s=%s", n, v);
    } else {
        def = xstrdup(n);
    }
    strarray_push(&((BuildTarget *)(intptr_t)t)->defines, def);
    return 0;
}
static long long impl_add_undef(long long t, long long name) {
    strarray_push(&((BuildTarget *)(intptr_t)t)->undefs, xstrdup((const char *)name));
    return 0;
}
static long long impl_add_cflag(long long t, long long flag) {
    strarray_push(&((BuildTarget *)(intptr_t)t)->cflags, xstrdup((const char *)flag));
    return 0;
}
static long long impl_add_ldflag(long long t, long long flag) {
    strarray_push(&((BuildTarget *)(intptr_t)t)->ldflags, xstrdup((const char *)flag));
    return 0;
}
// Internal helper: add a dependency edge.  link=1 → LinkWith (adds -l<dep>);
// link=0 → DependsOn (ordering only, no linker flag).
static void add_dep(BuildTarget *tgt, BuildTarget *d, int link) {
    if (tgt->deps_count >= tgt->deps_cap) {
        tgt->deps_cap = tgt->deps_cap ? tgt->deps_cap * 2 : 4;
        tgt->deps = realloc(tgt->deps, sizeof(*tgt->deps) * tgt->deps_cap);
        tgt->deps_link = realloc(tgt->deps_link, sizeof(*tgt->deps_link) * tgt->deps_cap);
        if (!tgt->deps || !tgt->deps_link)
            error("build: out of memory");
    }
    tgt->deps[tgt->deps_count] = d;
    tgt->deps_link[tgt->deps_count] = link;
    tgt->deps_count++;
}

static long long impl_link_with(long long t, long long dep) {
    add_dep((BuildTarget *)(intptr_t)t, (BuildTarget *)(intptr_t)dep, 1);
    return 0;
}

// DependsOn: ordering-only edge, no -l<dep> linker flag (#544).
static long long impl_depends_on(long long t, long long dep) {
    add_dep((BuildTarget *)(intptr_t)t, (BuildTarget *)(intptr_t)dep, 0);
    return 0;
}
static long long impl_add_lib(long long t, long long name) {
    strarray_push(&((BuildTarget *)(intptr_t)t)->libs, xstrdup((const char *)name));
    return 0;
}
static long long impl_add_libpath(long long t, long long path) {
    strarray_push(&((BuildTarget *)(intptr_t)t)->libpaths, xstrdup((const char *)path));
    return 0;
}

// ============================================================================
// #542 — source-set ergonomics
// ============================================================================

// AddSourcesGlob: expand a glob pattern (relative to build root) and add each
// matched file as a source.  POSIX glob(3) only; on non-POSIX platforms this
// is a no-op (a future Windows port can use FindFirstFile).
static long long impl_add_sources_glob(long long t, long long pattern) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    const char *pat = (const char *)pattern;
    if (!pat || !*pat) return 0;
#ifdef _POSIX_VERSION
    glob_t g;
    memset(&g, 0, sizeof(g));
    int rc = glob(pat, GLOB_NOSORT, NULL, &g);
    if (rc == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++)
            strarray_push(&tgt->sources, xstrdup(g.gl_pathv[i]));
    } else if (rc != GLOB_NOMATCH) {
        fprintf(stderr, "build: glob('%s') failed\n", pat);
    }
    globfree(&g);
#else
    (void)tgt;
    fprintf(stderr, "build: AddSourcesGlob not supported on this platform\n");
#endif
    return 0;
}

// AddSourceStr: write `content` to <out_dir>/gen/<name> and add it as a source.
// The name must end in .c (or a similar compilable extension).
static long long impl_add_source_str(long long t, long long name, long long content) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    Builder *ctx = s_ctx;
    const char *fname = (const char *)name;
    const char *body  = (const char *)content;
    if (!fname || !body || !ctx) return -1;

    // Build the output path: <out_dir>/gen/<name>
    char *gendir = join(ctx->out_dir, "gen");
    char *path = join(gendir, fname);
    free(gendir);

    if (!ctx->dry_run) {
        char *d = dir_of(path);
        if (mkdir_p(d) != 0) {
            fprintf(stderr, "build: failed to create gen dir for '%s'\n", fname);
            free(d); free(path); return -1;
        }
        free(d);
        FILE *f = fopen(path, "w");
        if (!f) {
            fprintf(stderr, "build: failed to write generated source '%s'\n", path);
            free(path); return -1;
        }
        fputs(body, f);
        fclose(f);
    }

    strarray_push(&tgt->sources, path); // path is now owned by the StringArray
    return 0;
}

// ExcludeSource: add a path or glob pattern to the target's exclude list.
// Exclusions are applied in compile_sources() via fnmatch().
static long long impl_exclude_source(long long t, long long path) {
    strarray_push(&((BuildTarget *)(intptr_t)t)->excludes, xstrdup((const char *)path));
    return 0;
}

// ============================================================================
// #543 — toolchain probing
// ============================================================================

// HaveTool: returns 1 if `name` is executable (found in PATH) and allowed,
// 0 otherwise.  Uses cccc_path_find_executable (silent; no error print).
static long long impl_have_tool(long long ctx, long long name) {
    (void)ctx;
    const char *tool = (const char *)name;
    if (!s_ctx || !tool) return 0;
    if (!tool_allowed(s_ctx, tool)) return 0;
    char *path = cccc_path_find_executable(tool);
    if (path) { free(path); return 1; }
    return 0;
}

// PkgConfig: run `pkg-config --cflags --libs <pkg>`, parse the output into
// the target's compile/link flag arrays.  Returns 0 on success.
// Flags starting with -I or -D go to cflags; everything else goes to ldflags.
static long long impl_pkg_config(long long t, long long pkg) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    const char *pkgname = (const char *)pkg;
    if (!tgt || !pkgname || !s_ctx) return -1;

    if (!tool_allowed(s_ctx, "pkg-config")) {
        fprintf(stderr, "build: PkgConfig: 'pkg-config' not in tool allowlist\n");
        return -1;
    }

#ifdef _POSIX_VERSION
    char *cflags_out = NULL;
    char *libs_out = NULL;
    int rc = 0;

    // Run pkg-config --cflags <pkg>
    {
        char *argv[] = { "pkg-config", "--cflags", (char *)pkgname, NULL };
        rc = run_capture(argv, &cflags_out);
        if (rc != 0) {
            fprintf(stderr, "build: PkgConfig: pkg-config --cflags %s failed (rc=%d)\n", pkgname, rc);
            free(cflags_out);
            return rc;
        }
    }

    // Run pkg-config --libs <pkg>
    {
        char *argv[] = { "pkg-config", "--libs", (char *)pkgname, NULL };
        rc = run_capture(argv, &libs_out);
        if (rc != 0) {
            fprintf(stderr, "build: PkgConfig: pkg-config --libs %s failed (rc=%d)\n", pkgname, rc);
            free(cflags_out); free(libs_out);
            return rc;
        }
    }

    // Parse cflags tokens → tgt->cflags
    if (cflags_out) {
        char *tok = strtok(cflags_out, " \t\n\r");
        while (tok) {
            if (*tok) strarray_push(&tgt->cflags, xstrdup(tok));
            tok = strtok(NULL, " \t\n\r");
        }
        free(cflags_out);
    }

    // Parse libs tokens → tgt->ldflags
    if (libs_out) {
        char *tok = strtok(libs_out, " \t\n\r");
        while (tok) {
            if (*tok) strarray_push(&tgt->ldflags, xstrdup(tok));
            tok = strtok(NULL, " \t\n\r");
        }
        free(libs_out);
    }

    return 0;
#else
    (void)tgt;
    fprintf(stderr, "build: PkgConfig not supported on this platform\n");
    return -1;
#endif
}

// ============================================================================
// #544 — custom build steps
// ============================================================================

// RunCustom: register a custom shell-command step as a DAG node.
static long long impl_run_custom(long long ctx, long long name, long long cmd) {
    (void)ctx;
    Builder *bctx = s_ctx;
    if (!bctx)
        error("build: RunCustom called outside a build run");
    BuildTarget *t = new_target(CCCC_TGT_CUSTOM, (const char *)name);
    t->command = xstrdup((const char *)cmd);
    return (long long)(intptr_t)t;
}

// ============================================================================
// Environment and filesystem helpers
// ============================================================================

// Intern a malloc'd string into the builder's capture pool so the pointer
// stays valid until the builder is torn down.
static const char *builder_intern(Builder *ctx, char *s) {
    if (!s) return NULL;
    if (ctx->captures_count >= ctx->captures_cap) {
        ctx->captures_cap = ctx->captures_cap ? ctx->captures_cap * 2 : 8;
        char **tmp = realloc(ctx->captures, ctx->captures_cap * sizeof(*ctx->captures));
        if (!tmp) { free(s); return NULL; }
        ctx->captures = tmp;
    }
    ctx->captures[ctx->captures_count++] = s;
    return s;
}

// GetEnv: return the value of an environment variable, or NULL if unset.
static long long impl_get_env(long long ctx, long long name) {
    (void)ctx;
    const char *var = (const char *)name;
    if (!var) return 0;
    return (long long)(intptr_t)getenv(var);
}

// CaptureCommand: run cmd via sh -c and return stdout (trailing whitespace
// stripped) as a NUL-terminated string owned by the builder, or NULL on failure.
static long long impl_capture_command(long long ctx, long long cmd) {
    (void)ctx;
    const char *command = (const char *)cmd;
    if (!s_ctx || !command) return 0;
    if (!tool_allowed(s_ctx, "CaptureCommand")) {
        fprintf(stderr, "build: CaptureCommand is not in the tool allowlist\n");
        return 0;
    }
#ifdef _POSIX_VERSION
    char *out = NULL;
    char *argv[] = { "sh", "-c", (char *)command, NULL };
    int rc = run_capture(argv, &out);
    if (rc != 0 || !out) { free(out); return 0; }
    // Strip trailing whitespace/newlines
    size_t len = strlen(out);
    while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r' ||
                       out[len-1] == ' '  || out[len-1] == '\t'))
        out[--len] = '\0';
    return (long long)(intptr_t)builder_intern(s_ctx, out);
#else
    return 0;
#endif
}

// FileExists: return 1 if path exists (any filesystem node type), 0 otherwise.
static long long impl_file_exists(long long ctx, long long path) {
    (void)ctx;
    const char *p = (const char *)path;
    if (!p) return 0;
#ifdef _POSIX_VERSION
    return access(p, F_OK) == 0 ? 1 : 0;
#else
    struct stat st;
    return stat(p, &st) == 0 ? 1 : 0;
#endif
}

// ============================================================================
// #559 — FindTool, build options, AddFramework
// ============================================================================

// FindTool: like HaveTool but returns the full executable path (or NULL).
// Useful when the path needs to be passed to RunCustom or embedded in a define.
static long long impl_find_tool(long long ctx, long long name) {
    (void)ctx;
    const char *tool = (const char *)name;
    if (!s_ctx || !tool) return 0;
    if (!tool_allowed(s_ctx, tool)) return 0;
    char *path = cccc_path_find_executable(tool);
    if (!path) return 0;
    return (long long)(intptr_t)builder_intern(s_ctx, path);
}

// GetBuildOption: return the value of a --build-option=key=value option, or NULL.
static long long impl_get_build_option(long long ctx, long long name) {
    (void)ctx;
    const char *key = (const char *)name;
    if (!s_ctx || !key) return 0;
    size_t klen = strlen(key);
    for (int i = 0; i < s_ctx->build_options_count; i++) {
        const char *opt = s_ctx->build_options[i];
        if (strncmp(opt, key, klen) == 0 && opt[klen] == '=')
            return (long long)(intptr_t)(opt + klen + 1);
    }
    return 0;
}

// HaveBuildOption: return 1 if --build-option=key[=...] was given.
static long long impl_have_build_option(long long ctx, long long name) {
    (void)ctx;
    const char *key = (const char *)name;
    if (!s_ctx || !key) return 0;
    size_t klen = strlen(key);
    for (int i = 0; i < s_ctx->build_options_count; i++) {
        const char *opt = s_ctx->build_options[i];
        if (strncmp(opt, key, klen) == 0 && (opt[klen] == '=' || opt[klen] == '\0'))
            return 1;
    }
    return 0;
}

// BuildArgc: number of user args forwarded via -- on the CLI (#558).
static long long impl_build_argc(long long ctx) {
    (void)ctx;
    return s_ctx ? s_ctx->user_args_count : 0;
}

// BuildArgv: return the i-th user arg (0-based), or NULL if out of range (#558).
static long long impl_build_argv(long long ctx, long long i) {
    (void)ctx;
    if (!s_ctx || i < 0 || i >= s_ctx->user_args_count) return 0;
    return (long long)(intptr_t)s_ctx->user_args[i];
}

// AddFramework: macOS -framework <name> shorthand (cleaner than two AddLdFlag calls).
// Adds two separate linker tokens: "-framework" and the framework name.
// On non-Apple platforms the tokens are still added; the linker will reject them,
// which is the expected behaviour (build scripts should guard with BuildHost).
static long long impl_add_framework(long long t, long long name) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    const char *fw = (const char *)name;
    if (!tgt || !fw) return -1;
    strarray_push(&tgt->ldflags, xstrdup("-framework"));
    strarray_push(&tgt->ldflags, xstrdup(fw));
    return 0;
}

// ============================================================================
// #560 — SetInstallPrefix / InstallArtifact / BuildWantsInstall
// ============================================================================

static long long impl_set_install_prefix(long long ctx, long long path) {
    (void)ctx;
    const char *p = (const char *)path;
    if (!s_ctx || !p) return 0;
    free(s_ctx->install_prefix);
    s_ctx->install_prefix = xstrdup(p);
    return 0;
}

static long long impl_install_artifact(long long ctx, long long t) {
    (void)ctx;
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    if (!s_ctx || !tgt) return 0;
    if (!s_ctx->build_install) return 0; // no-op without --build-install
    if (s_ctx->install_count >= s_ctx->install_cap) {
        s_ctx->install_cap = s_ctx->install_cap ? s_ctx->install_cap * 2 : 4;
        BuildTarget **tmp = realloc(s_ctx->install_targets,
                                    s_ctx->install_cap * sizeof(*s_ctx->install_targets));
        if (!tmp) return -1;
        s_ctx->install_targets = tmp;
    }
    s_ctx->install_targets[s_ctx->install_count++] = tgt;
    return 0;
}

static long long impl_build_wants_install(long long ctx) {
    (void)ctx;
    return s_ctx ? (long long)s_ctx->build_install : 0;
}

// ============================================================================
// #561 — DirExists, GlobFiles, ReadFile, WriteFile
// ============================================================================

// DirExists: 1 if path exists and is a directory, 0 otherwise.
static long long impl_dir_exists(long long ctx, long long path) {
    (void)ctx;
    const char *p = (const char *)path;
    if (!p) return 0;
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

// GlobFiles: expand a glob pattern and return a NULL-terminated array of paths,
// or NULL on no match or non-POSIX platforms.
static long long impl_glob_files(long long ctx, long long pattern) {
    (void)ctx;
    if (!s_ctx || !pattern) return 0;
#ifdef _POSIX_VERSION
    glob_t g;
    int rc = glob((const char *)pattern, GLOB_TILDE, NULL, &g);
    if (rc != 0) { if (rc != GLOB_NOMATCH) globfree(&g); return 0; }
    char **arr = malloc((g.gl_pathc + 1) * sizeof(char *));
    if (!arr) { globfree(&g); return 0; }
    for (size_t i = 0; i < g.gl_pathc; i++)
        arr[i] = (char *)builder_intern(s_ctx, xstrdup(g.gl_pathv[i]));
    arr[g.gl_pathc] = NULL;
    globfree(&g);
    // Intern the array pointer itself so it is freed when the builder is torn down.
    builder_intern(s_ctx, (char *)arr);
    return (long long)(intptr_t)arr;
#else
    return 0;
#endif
}

// ReadFile: read a small file into a NUL-terminated string (max 4 MB).
// Returns NULL on error or if the file exceeds the size limit.
static long long impl_read_file(long long ctx, long long path) {
    (void)ctx;
    if (!s_ctx || !path) return 0;
    FILE *f = fopen((const char *)path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    rewind(f);
    if (sz < 0 || sz > 4 * 1024 * 1024) { fclose(f); return 0; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f); free(buf); return 0;
    }
    fclose(f);
    buf[sz] = '\0';
    return (long long)(intptr_t)builder_intern(s_ctx, buf);
}

// WriteFile: write content to path, creating parent directories as needed.
// Returns 0 on success, -1 on failure.
static long long impl_write_file(long long ctx, long long path, long long content) {
    (void)ctx;
    const char *p = (const char *)path;
    const char *c = (const char *)content;
    if (!p || !c) return -1;
    char *d = dir_of(p);
    if (mkdir_p(d) != 0) { free(d); return -1; }
    free(d);
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    size_t n = strlen(c);
    int ok = fwrite(c, 1, n, f) == n;
    fclose(f);
    return ok ? 0 : -1;
}

static long long impl_build_root(long long ctx) {
    (void)ctx;
    return (long long)(intptr_t)(s_ctx ? s_ctx->root : "");
}
static long long impl_build_out_dir(long long ctx) {
    (void)ctx;
    return (long long)(intptr_t)(s_ctx ? s_ctx->out_dir : "");
}
static long long impl_build_host(long long ctx) {
    (void)ctx;
    return (long long)(intptr_t)(s_ctx ? s_ctx->host : CCCC_BUILD_HOST);
}
static long long impl_build_verbose(long long ctx) {
    (void)ctx;
    return s_ctx ? s_ctx->verbose : 0;
}

// run impls are defined after the runner, below.
static long long impl_build_run(long long ctx, long long t);
static long long impl_build_run_all(long long ctx);
static long long impl_build_run_default(long long ctx);

// ============================================================================
// #547 — cross-compilation FFI impls
// ============================================================================

static long long impl_set_toolchain(long long t, long long cc) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    const char *compiler = (const char *)cc;
    if (!tgt || !compiler) return 0;
    free(tgt->cc_override);
    tgt->cc_override = xstrdup(compiler);
    return 0;
}

static long long impl_set_target_triple(long long t, long long triple) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    const char *tr = (const char *)triple;
    if (!tgt || !tr) return 0;
    free(tgt->target_triple);
    tgt->target_triple = xstrdup(tr);
    return 0;
}

static long long impl_build_target_triple(long long ctx) {
    (void)ctx;
    return (long long)(intptr_t)(s_ctx ? s_ctx->cross_triple : NULL);
}

// ============================================================================
// #548 — profile FFI impls
// ============================================================================

static long long impl_set_profile(long long t, long long p) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    const char *prof = (const char *)p;
    if (!tgt || !prof) return 0;
    free(tgt->profile);
    tgt->profile = xstrdup(prof);
    return 0;
}

static long long impl_build_profile(long long ctx) {
    (void)ctx;
    return (long long)(intptr_t)(s_ctx ? s_ctx->profile : NULL);
}

// ============================================================================
// #540 — build_target factory reflection
// ============================================================================

// BuildTargetCount: returns the number of [[cccc::build_target]] factories
// registered in the current build script.
static long long impl_target_count(long long ctx) {
    (void)ctx;
    return s_ctx ? (long long)s_ctx->factory_count : 0;
}

// BuildTargetName: returns the name of factory i (0-based).  Returns NULL for
// out-of-range indices.
static long long impl_target_name(long long ctx, long long i) {
    (void)ctx;
    if (!s_ctx || i < 0 || (int)i >= s_ctx->factory_count)
        return 0;
    return (long long)(intptr_t)s_ctx->factory_names[(int)i];
}

// Forward declarations for filesystem helpers defined later in this file.
static long long impl_set_cwd(long long ctx, long long path);
static long long impl_get_cwd(long long ctx);
static long long impl_copy_file(long long ctx, long long src, long long dst);
static long long impl_move_file(long long ctx, long long src, long long dst);
static long long impl_delete_file(long long ctx, long long path);
static long long impl_mkdir(long long ctx, long long path);
static long long impl_delete_dir(long long ctx, long long path);

void cc_load_build_runtime(VirtualMachine *vm) {
    cc_register_cfunc(vm, "__builtin_build_executable",    (void *)impl_executable,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_static_lib",    (void *)impl_static_lib,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_dynamic_lib",   (void *)impl_dynamic_lib,        2, 0);
    cc_register_cfunc(vm, "__builtin_build_set_output",    (void *)impl_set_output,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_target_output", (void *)impl_target_output,      1, 0);
    cc_register_cfunc(vm, "__builtin_build_declare_output",(void *)impl_declare_output,     2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_source",    (void *)impl_add_source,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_sources_glob",(void*)impl_add_sources_glob,  2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_source_str",(void *)impl_add_source_str,     3, 0);
    cc_register_cfunc(vm, "__builtin_build_exclude_source",(void *)impl_exclude_source,     2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_include",   (void *)impl_add_include,        2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_define",    (void *)impl_add_define,         3, 0);
    cc_register_cfunc(vm, "__builtin_build_add_undef",     (void *)impl_add_undef,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_cflag",     (void *)impl_add_cflag,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_ldflag",    (void *)impl_add_ldflag,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_set_target_env",(void *)impl_set_target_env,     3, 0);
    cc_register_cfunc(vm, "__builtin_build_link_with",     (void *)impl_link_with,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_depends_on",    (void *)impl_depends_on,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_lib",       (void *)impl_add_lib,            2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_libpath",   (void *)impl_add_libpath,        2, 0);
    cc_register_cfunc(vm, "__builtin_build_have_tool",       (void *)impl_have_tool,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_pkg_config",      (void *)impl_pkg_config,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_run_custom",      (void *)impl_run_custom,         3, 0);
    cc_register_cfunc(vm, "__builtin_build_get_env",         (void *)impl_get_env,            2, 0);
    cc_register_cfunc(vm, "__builtin_build_capture_command", (void *)impl_capture_command,    2, 0);
    cc_register_cfunc(vm, "__builtin_build_file_exists",     (void *)impl_file_exists,        2, 0);
    cc_register_cfunc(vm, "__builtin_build_root",          (void *)impl_build_root,         1, 0);
    cc_register_cfunc(vm, "__builtin_build_out_dir",       (void *)impl_build_out_dir,      1, 0);
    cc_register_cfunc(vm, "__builtin_build_host",          (void *)impl_build_host,         1, 0);
    cc_register_cfunc(vm, "__builtin_build_verbose",       (void *)impl_build_verbose,      1, 0);
    cc_register_cfunc(vm, "__builtin_build_run",           (void *)impl_build_run,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_run_all",       (void *)impl_build_run_all,      1, 0);
    cc_register_cfunc(vm, "__builtin_build_run_default",   (void *)impl_build_run_default,  1, 0);
    cc_register_cfunc(vm, "__builtin_build_target_count",  (void *)impl_target_count,       1, 0);
    cc_register_cfunc(vm, "__builtin_build_target_name",   (void *)impl_target_name,        2, 0);
    cc_register_cfunc(vm, "__builtin_build_set_profile",       (void *)impl_set_profile,        2, 0);
    cc_register_cfunc(vm, "__builtin_build_profile",           (void *)impl_build_profile,      1, 0);
    cc_register_cfunc(vm, "__builtin_build_set_toolchain",     (void *)impl_set_toolchain,      2, 0);
    cc_register_cfunc(vm, "__builtin_build_set_target_triple", (void *)impl_set_target_triple,  2, 0);
    cc_register_cfunc(vm, "__builtin_build_target_triple",     (void *)impl_build_target_triple,1, 0);
    // #559
    cc_register_cfunc(vm, "__builtin_build_find_tool",         (void *)impl_find_tool,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_get_build_option",  (void *)impl_get_build_option,   2, 0);
    cc_register_cfunc(vm, "__builtin_build_have_build_option", (void *)impl_have_build_option,  2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_framework",     (void *)impl_add_framework,      2, 0);
    // #560
    cc_register_cfunc(vm, "__builtin_build_set_install_prefix",(void *)impl_set_install_prefix, 2, 0);
    cc_register_cfunc(vm, "__builtin_build_install_artifact",  (void *)impl_install_artifact,   2, 0);
    cc_register_cfunc(vm, "__builtin_build_wants_install",     (void *)impl_build_wants_install, 1, 0);
    // #558
    cc_register_cfunc(vm, "__builtin_build_argc",              (void *)impl_build_argc,          1, 0);
    cc_register_cfunc(vm, "__builtin_build_argv",              (void *)impl_build_argv,          2, 0);
    // #561
    cc_register_cfunc(vm, "__builtin_build_dir_exists",        (void *)impl_dir_exists,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_glob_files",        (void *)impl_glob_files,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_read_file",         (void *)impl_read_file,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_write_file",        (void *)impl_write_file,         3, 0);
    // #569
    cc_register_cfunc(vm, "__builtin_build_set_cwd",           (void *)impl_set_cwd,            2, 0);
    cc_register_cfunc(vm, "__builtin_build_get_cwd",           (void *)impl_get_cwd,            1, 0);
    cc_register_cfunc(vm, "__builtin_build_copy_file",         (void *)impl_copy_file,          3, 0);
    cc_register_cfunc(vm, "__builtin_build_move_file",         (void *)impl_move_file,          3, 0);
    cc_register_cfunc(vm, "__builtin_build_delete_file",       (void *)impl_delete_file,        2, 0);
    cc_register_cfunc(vm, "__builtin_build_mkdir",             (void *)impl_mkdir,              2, 0);
    cc_register_cfunc(vm, "__builtin_build_delete_dir",        (void *)impl_delete_dir,         2, 0);
}

// ============================================================================
// Header injection
// ============================================================================

Token *cc_inject_build_header(VirtualMachine *vm) {
    char *src = get_std_header("building.h");
    if (!src)
        error("could not load embedded building.h — run `make stdlib` to regenerate src/std.c");
    Token *toks = tokenize_string(vm, "<building.h>", src);
    return preprocess(vm, toks);
}

// ============================================================================
// Host-side runner (#535): topo-sort + per-target cc/ar/ld
// ============================================================================

// ============================================================================
// #547 — cross-compilation helpers
// ============================================================================

// Determine the effective compiler binary for a target:
//   1. t->cc_override (per-target SetToolchain)
//   2. ctx->cross_cc  (global --build-cc)
//   3. cccc_find_native_cc() (system default)
// Returns a newly-allocated string; caller must free.
static char *effective_cc_for_target(const Builder *ctx, const BuildTarget *t) {
    if (t->cc_override && *t->cc_override)
        return xstrdup(t->cc_override);
    if (ctx->cross_cc && *ctx->cross_cc)
        return xstrdup(ctx->cross_cc);
    return cccc_find_native_cc();
}

// Push --target=<triple> to args when a cross-compilation triple is in effect.
// Effective triple: t->target_triple ?? ctx->cross_triple.
// --target is accepted by clang; gcc-style cross-compilers use a prefixed
// binary (SetToolchain) and do not need this flag.
static void push_cross_flags(ArgVec *args, const Builder *ctx, const BuildTarget *t,
                             StringArray *owned) {
    const char *triple = t->target_triple ? t->target_triple : ctx->cross_triple;
    if (!triple || !*triple) return;
    // Must be a single joined "--target=<triple>" token (#842): clang
    // accepts "--target=<triple>" or "-target <triple>" (single dash, two
    // tokens) but NOT "--target <triple>" (double dash, two tokens) --
    // verified against real clang, which reports "unknown argument
    // '--target'; did you mean '-target'?" for that form. This was never
    // exercised by a real (non-dry-run) build before #842 added
    // macos_x86_64 to build.c.
    size_t len = strlen(triple) + 10;
    char *f = malloc(len);
    if (!f)
        error("build: out of memory");
    snprintf(f, len, "--target=%s", triple);
    strarray_push(owned, f); // caller frees `owned` after run_step
    argv_push(args, f);
}

// ============================================================================
// #548 — build profiles
// ============================================================================

// Push the compile-side flags for the named profile.  Called before the
// target's own cflags so the target can override (e.g. AddCFlag("-O3")).
// Profile flags intentionally do NOT include -DNDEBUG here; that is added
// as a define flag in push_profile_defines() below.
static void push_profile_cflags(ArgVec *args, const char *profile) {
    if (!profile) return;
    if (strcmp(profile, "debug") == 0) {
        argv_push(args, "-g");
        argv_push(args, "-O0");
    } else if (strcmp(profile, "release") == 0) {
        argv_push(args, "-O2");
    } else if (strcmp(profile, "relwithdebinfo") == 0) {
        argv_push(args, "-O2");
        argv_push(args, "-g");
    } else if (strcmp(profile, "minsizerel") == 0) {
        argv_push(args, "-Os");
    } else {
        fprintf(stderr, "build: unknown profile '%s' (valid: debug, release, "
                "relwithdebinfo, minsizerel)\n", profile);
    }
}

// Push -DNDEBUG for profiles that use it.
static void push_profile_defines(ArgVec *args, const char *profile) {
    if (!profile) return;
    if (strcmp(profile, "release") == 0 ||
        strcmp(profile, "relwithdebinfo") == 0 ||
        strcmp(profile, "minsizerel") == 0) {
        argv_push(args, "-DNDEBUG");
    }
}

// Append the CLI defaults and a target's own compile flags to an ArgVec.
// `owned` accumulates heap strings (e.g. "-DFOO") that must outlive the spawn.
static void push_compile_flags(ArgVec *args, const Builder *ctx,
                               const BuildTarget *t, StringArray *owned) {
    const CcNativeCompileArgs *d = ctx->defaults;
    if (d && d->std_arg) {
        char *f = malloc(strlen(d->std_arg) + 8);
        snprintf(f, strlen(d->std_arg) + 8, "-std=%s", d->std_arg);
        strarray_push(owned, f);
        argv_push(args, f);
    }
    for (int i = 0; d && i < d->inc_paths_count; i++) {
        argv_push(args, "-I");
        argv_push(args, d->inc_paths[i]);
    }
    for (int i = 0; d && i < d->defines_count; i++) {
        char *f = malloc(strlen(d->defines[i]) + 3);
        snprintf(f, strlen(d->defines[i]) + 3, "-D%s", d->defines[i]);
        strarray_push(owned, f);
        argv_push(args, f);
    }
    for (int i = 0; d && i < d->undefs_count; i++) {
        char *f = malloc(strlen(d->undefs[i]) + 3);
        snprintf(f, strlen(d->undefs[i]) + 3, "-U%s", d->undefs[i]);
        strarray_push(owned, f);
        argv_push(args, f);
    }
    // Profile flags: pushed before target-specific flags so targets can override.
    // Effective profile = per-target override else global ctx->profile.
    const char *profile = t->profile ? t->profile : ctx->profile;
    push_profile_cflags(args, profile);
    push_profile_defines(args, profile);

    // Cross-compilation triple (#547): --target=<triple> when set.
    push_cross_flags(args, ctx, t, owned);

    for (int i = 0; i < t->includes.len; i++) {
        argv_push(args, "-I");
        argv_push(args, t->includes.data[i]);
    }
    for (int i = 0; i < t->defines.len; i++) {
        char *f = malloc(strlen(t->defines.data[i]) + 3);
        snprintf(f, strlen(t->defines.data[i]) + 3, "-D%s", t->defines.data[i]);
        strarray_push(owned, f);
        argv_push(args, f);
    }
    for (int i = 0; i < t->undefs.len; i++) {
        char *f = malloc(strlen(t->undefs.data[i]) + 3);
        snprintf(f, strlen(t->undefs.data[i]) + 3, "-U%s", t->undefs.data[i]);
        strarray_push(owned, f);
        argv_push(args, f);
    }
    for (int i = 0; i < t->cflags.len; i++)
        argv_push(args, t->cflags.data[i]);
}

// Variant for kind=bytecode targets: forward only flags that cccc understands.
// Skips native cflags/ldflags/profile flags; forwards -I, -D, -U, and --std.
static void push_compile_flags_bytecode(ArgVec *args, const Builder *ctx,
                                        const BuildTarget *t, StringArray *owned) {
    const CcNativeCompileArgs *d = ctx->defaults;
    if (d && d->std_arg) {
        char *f = malloc(strlen(d->std_arg) + 10);
        snprintf(f, strlen(d->std_arg) + 10, "--std=%s", d->std_arg);
        strarray_push(owned, f);
        argv_push(args, f);
    }
    for (int i = 0; d && i < d->inc_paths_count; i++) {
        argv_push(args, "-I");
        argv_push(args, d->inc_paths[i]);
    }
    for (int i = 0; d && i < d->defines_count; i++) {
        char *f = malloc(strlen(d->defines[i]) + 3);
        snprintf(f, strlen(d->defines[i]) + 3, "-D%s", d->defines[i]);
        strarray_push(owned, f);
        argv_push(args, f);
    }
    for (int i = 0; d && i < d->undefs_count; i++) {
        char *f = malloc(strlen(d->undefs[i]) + 3);
        snprintf(f, strlen(d->undefs[i]) + 3, "-U%s", d->undefs[i]);
        strarray_push(owned, f);
        argv_push(args, f);
    }
    for (int i = 0; i < t->includes.len; i++) {
        argv_push(args, "-I");
        argv_push(args, t->includes.data[i]);
    }
    for (int i = 0; i < t->defines.len; i++) {
        char *f = malloc(strlen(t->defines.data[i]) + 3);
        snprintf(f, strlen(t->defines.data[i]) + 3, "-D%s", t->defines.data[i]);
        strarray_push(owned, f);
        argv_push(args, f);
    }
    for (int i = 0; i < t->undefs.len; i++) {
        char *f = malloc(strlen(t->undefs.data[i]) + 3);
        snprintf(f, strlen(t->undefs.data[i]) + 3, "-U%s", t->undefs.data[i]);
        strarray_push(owned, f);
        argv_push(args, f);
    }
}

static void free_strarray(StringArray *a) {
    for (int i = 0; i < a->len; i++)
        free(a->data[i]);
    free(a->data);
    a->data = NULL;
    a->len = a->capacity = 0;
}

// Print a command line the same way it would be executed (for progress / dry-run).
static void print_cmd(int n, int total, char *const argv[]) {
    printf("[%d/%d]", n, total);
    for (int i = 0; argv[i]; i++)
        printf(" %s", argv[i]);
    printf("\n");
}

// Run (or, in dry-run, just print) one toolchain command.  Returns its exit
// code.  `t` may be NULL (no per-target environment overrides); when it has
// SetTargetEnv() entries (t->env), they are applied on top of the process
// environment for this child only — e.g. AFL_USE_ASAN=1 for an afl-asan
// target (#842).
static int run_step(Builder *ctx, int n, int total, ArgVec *args, const BuildTarget *t) {
    if (!ctx->quiet || ctx->verbose || ctx->dry_run) {
        print_cmd(n, total, (char *const *)args->data);
        fflush(stdout);
    }
    if (ctx->dry_run)
        return 0;
    if (t && t->env.len > 0)
        return run_argv_env((char *const *)args->data, (char *const *)t->env.data);
    return run_argv((char *const *)args->data);
}

// Returns 1 if `src` matches any pattern in the target's exclude list.
static int source_is_excluded(const BuildTarget *t, const char *src) {
#ifdef _POSIX_VERSION
    for (int i = 0; i < t->excludes.len; i++) {
        // Try fnmatch first (glob pattern), then exact path comparison.
        if (fnmatch(t->excludes.data[i], src, FNM_PATHNAME) == 0)
            return 1;
        if (strcmp(t->excludes.data[i], src) == 0)
            return 1;
    }
#else
    for (int i = 0; i < t->excludes.len; i++)
        if (strcmp(t->excludes.data[i], src) == 0)
            return 1;
#endif
    return 0;
}

// ============================================================================
// Incremental build cache (#546)
// ============================================================================

// FNV-1a 64-bit: simple, dependency-free content hash used for the CAS key.
#define CACHE_FNV_OFFSET 0xcbf29ce484222325ULL
#define CACHE_FNV_PRIME  0x00000100000001b3ULL

// Host architecture tag folded into the native object cache (#730). A plain
// native build emits no --target flag, so two same-OS cccc binaries built
// for different architectures (e.g. native arm64 macOS vs a cccc-macos-x86_64
// binary run under Rosetta) can produce identical argv+source-content cache
// keys if a build/cache directory is reused between them -- the second build
// then links against wrong-arch object files. The compiler/linker cccc
// spawns inherit its own process architecture, so the arch cccc itself was
// built for is a direct, cheap discriminator (no subprocess query needed).
// Real cross builds (--build-cc/--build-triple) already fold their target
// triple into argv and are unaffected. Bytecode (.c4) output is portable
// across same-OS architectures, so this tag is only used for native objects.
#if defined(__x86_64__) || defined(__amd64__)
#define CCCC_HOST_ARCH_TAG "x86_64"
#elif defined(__aarch64__) || defined(__arm64__)
#define CCCC_HOST_ARCH_TAG "aarch64"
#elif defined(__arm__)
#define CCCC_HOST_ARCH_TAG "arm"
#else
#define CCCC_HOST_ARCH_TAG "unknown-arch"
#endif

static uint64_t fnv1a_update(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= CACHE_FNV_PRIME; }
    return h;
}

static uint64_t fnv1a_file(const char *path, uint64_t h) {
    FILE *f = fopen(path, "rb");
    if (!f) return h;
    unsigned char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        h = fnv1a_update(h, buf, n);
    fclose(f);
    return h;
}

// Build a 64-bit cache key from the compile command + source content.
// Skips the -o <path> pair so the key is output-path-agnostic. Folds in
// CCCC_HOST_ARCH_TAG so a cross-arch cache/build-dir reuse misses (#730).
static uint64_t source_cache_key(const char *src, char *const *argv) {
    uint64_t h = CACHE_FNV_OFFSET;
    h = fnv1a_update(h, CCCC_HOST_ARCH_TAG, strlen(CCCC_HOST_ARCH_TAG));
    h = fnv1a_update(h, "\0", 1);
    for (int i = 0; argv[i]; i++) {
        if (strcmp(argv[i], "-o") == 0) { i++; continue; } // skip -o <path>
        h = fnv1a_update(h, argv[i], strlen(argv[i]));
        h = fnv1a_update(h, "\0", 1); // arg separator
    }
    return fnv1a_file(src, h);
}

// Returns 1 if the existing ofile is at least as new as src (mtime fast path).
static int ofile_is_current(const char *ofile, const char *src) {
    struct stat o_st, s_st;
    if (stat(ofile, &o_st) != 0 || stat(src, &s_st) != 0) return 0;
    return o_st.st_mtime >= s_st.st_mtime;
}

// Per-target arch stamp (<objdir>/.cccc-arch): guards the Level 1 mtime fast
// path against serving objects that a different-arch cccc binary compiled
// into the same reused build/cache directory (#730). The mtime check alone
// never consults the cache key, so without this stamp it would happily
// "cache hit" a wrong-arch .o that is merely newer than its source.
static int arch_stamp_matches(const char *objdir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/.cccc-arch", objdir);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[64] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return strcmp(buf, CCCC_HOST_ARCH_TAG) == 0;
}

static void arch_stamp_write(const char *objdir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/.cccc-arch", objdir);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs(CCCC_HOST_ARCH_TAG, f);
    fclose(f);
}

// CAS layout: <cache_dir>/<key[0:2]>/<key_hex><ext>
static void cache_entry_path(char *buf, size_t len,
                              const char *cache_dir, uint64_t key,
                              const char *ext) {
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)key);
    snprintf(buf, len, "%s/%.2s/%s%s", cache_dir, hex, hex, ext);
}

// Simple binary file copy; returns 0 on success, -1 on error.
static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[8192];
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    fclose(in);
    fclose(out);
    if (rc != 0) remove(dst);
    return rc;
}

// ============================================================================
// Build filesystem helpers (#569)
// ============================================================================

static long long impl_set_cwd(long long ctx, long long path) {
    (void)ctx;
    const char *p = (const char *)path;
    if (!s_ctx || !p) return -1;
    // Save original CWD on first call for auto-restore at teardown.
    if (!s_ctx->original_cwd) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf)))
            s_ctx->original_cwd = xstrdup(buf);
    }
    return chdir(p) == 0 ? 0 : -1;
}

static long long impl_get_cwd(long long ctx) {
    (void)ctx;
    if (!s_ctx) return 0;
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) return 0;
    char *copy = strdup(buf);
    if (!copy) return 0;
    return (long long)(intptr_t)builder_intern(s_ctx, copy);
}

static long long impl_copy_file(long long ctx, long long src, long long dst) {
    (void)ctx;
    const char *s = (const char *)src, *d = (const char *)dst;
    if (!s || !d) return -1;
    return copy_file(s, d);
}

static long long impl_move_file(long long ctx, long long src, long long dst) {
    (void)ctx;
    const char *s = (const char *)src, *d = (const char *)dst;
    if (!s || !d) return -1;
    if (rename(s, d) == 0) return 0;
    // Cross-device move: copy then delete.
    if (errno == EXDEV) {
        if (copy_file(s, d) != 0) return -1;
        if (unlink(s) != 0) { remove(d); return -1; }
        return 0;
    }
    return -1;
}

static long long impl_delete_file(long long ctx, long long path) {
    (void)ctx;
    const char *p = (const char *)path;
    if (!p) return -1;
    return unlink(p) == 0 ? 0 : -1;
}

static long long impl_mkdir(long long ctx, long long path) {
    (void)ctx;
    const char *p = (const char *)path;
    if (!p) return -1;
    return mkdir_p(p);
}

// Recursive rm-rf; does not follow symlinks out of the tree.
static int delete_dir_recursive(const char *path) {
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *e;
    int rc = 0;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char child[4096];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        struct stat st;
        if (lstat(child, &st) != 0) { rc = -1; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (delete_dir_recursive(child) != 0) rc = -1;
        } else {
            if (unlink(child) != 0) rc = -1;
        }
    }
    closedir(d);
    if (rc == 0 && rmdir(path) != 0) rc = -1;
    return rc;
}

static long long impl_delete_dir(long long ctx, long long path) {
    (void)ctx;
    const char *p = (const char *)path;
    if (!p) return -1;
    return delete_dir_recursive(p);
}

// Try to restore ofile from the CAS. Returns 1 on hit, 0 on miss.
static int cache_lookup(const char *cache_dir, uint64_t key,
                        const char *ofile, const char *ext) {
    char cpath[2048];
    cache_entry_path(cpath, sizeof(cpath), cache_dir, key, ext);
    struct stat st;
    if (stat(cpath, &st) != 0) return 0;
    char *d = dir_of(ofile);
    mkdir_p(d);
    free(d);
    return copy_file(cpath, ofile) == 0;
}

// Store compiled ofile into the CAS (best-effort; errors are silently ignored).
static void cache_store(const char *cache_dir, uint64_t key,
                        const char *ofile, const char *ext) {
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)key);
    char prefix[2048];
    snprintf(prefix, sizeof(prefix), "%s/%.2s", cache_dir, hex);
    if (mkdir_p(prefix) != 0) return;
    char cpath[2048];
    cache_entry_path(cpath, sizeof(cpath), cache_dir, key, ext);
    copy_file(ofile, cpath);
}

// Returns 1 if `out` is newer than every source in `srcs` (mtime fast path
// for whole-target bytecode output: .c4 / .c4a / .c4d).
static int bytecode_output_is_current(const char *out, StringArray *srcs) {
    struct stat o_st;
    if (stat(out, &o_st) != 0) return 0;
    for (int i = 0; i < srcs->len; i++) {
        struct stat s_st;
        if (stat(srcs->data[i], &s_st) != 0) return 0;
        if (o_st.st_mtime < s_st.st_mtime) return 0;
    }
    return srcs->len > 0;
}

// Per-target cache key for bytecode: FNV-1a over cccc argv (excluding -o <path>)
// + all aggregated source file contents.  The "bytecode\0" prefix namespaces
// these keys away from native .o keys so both can share a cache directory.
static uint64_t bytecode_target_cache_key(char *const *argv, StringArray *srcs) {
    uint64_t h = CACHE_FNV_OFFSET;
    static const char ns[] = "bytecode\0";
    h = fnv1a_update(h, ns, sizeof(ns));
    for (int i = 0; argv[i]; i++) {
        if (strcmp(argv[i], "-o") == 0) { i++; continue; }
        h = fnv1a_update(h, argv[i], strlen(argv[i]));
        h = fnv1a_update(h, "\0", 1);
    }
    for (int i = 0; i < srcs->len; i++)
        h = fnv1a_file(srcs->data[i], h);
    return h;
}

// Compile one target's sources to object files; collect the .o paths in `objs`.
// Sources matching the target's exclude list are silently skipped.
// Returns 0 on success.
static int compile_sources(Builder *ctx, const char *cc,
                           BuildTarget *t, const char *objdir,
                           StringArray *objs, int *step, int total) {
    StringArray owned = {0};

    // Level 1 (mtime fast path) is only trusted when this objdir's arch
    // stamp matches the arch cccc itself is running as; otherwise a reused
    // build dir from a different-arch cccc binary would look "up to date"
    // by mtime alone (#730). On mismatch/first-use, (re)write the stamp so
    // subsequent builds in this objdir can use the fast path again.
    int arch_ok = ctx->cache_dir && arch_stamp_matches(objdir);
    if (ctx->cache_dir && !arch_ok && !ctx->dry_run)
        arch_stamp_write(objdir);

#ifdef _POSIX_VERSION
    int jobs = ctx->jobs > 1 ? ctx->jobs : 1;
    if (jobs > 1 && !ctx->dry_run) {
        // Parallel pid pool: launch up to `jobs` cc -c children at once.
        typedef struct { pid_t pid; char *ofile; uint64_t cache_key; } Job;
        Job *pool = calloc(jobs, sizeof(Job));
        if (!pool) goto serial_fallback;

        int in_flight = 0;
        int any_failed = 0;

        for (int i = 0; i <= t->sources.len; i++) {
            // Drain one slot when the pool is full, or drain all on the final pass.
            while (in_flight > 0 && (in_flight >= jobs || i == t->sources.len)) {
                int status;
                pid_t done = waitpid(-1, &status, 0);
                if (done < 0) break;
                for (int j = 0; j < jobs; j++) {
                    if (pool[j].pid != done) continue;
                    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                    if (exit_code != 0) {
                        fprintf(stderr, "build: compile failed (exit %d)\n", exit_code);
                        any_failed = 1;
                        free(pool[j].ofile);
                    } else {
                        if (ctx->cache_dir)
                            cache_store(ctx->cache_dir, pool[j].cache_key, pool[j].ofile, ".o");
                        strarray_push(objs, pool[j].ofile); // transfer ownership
                    }
                    pool[j].pid = 0;
                    pool[j].ofile = NULL;
                    pool[j].cache_key = 0;
                    in_flight--;
                    break;
                }
            }

            if (i == t->sources.len) break;
            if (source_is_excluded(t, t->sources.data[i])) continue;
            if (any_failed) continue; // drain remaining, don't launch more

            char *stem = stem_of(t->sources.data[i]);
            char ofile[1024];
            snprintf(ofile, sizeof(ofile), "%s/%s.o", objdir, stem);
            free(stem);

            // Level 1: mtime check — skip recompile when ofile is up to date.
            if (ctx->cache_dir && arch_ok && ofile_is_current(ofile, t->sources.data[i])) {
                if (!ctx->quiet || ctx->verbose)
                    printf("[%d/%d] (cached) %s\n", ++(*step), total, t->sources.data[i]);
                else
                    ++(*step);
                strarray_push(objs, xstrdup(ofile));
                continue;
            }

            ArgVec a = {0};
            argv_push(&a, cc);
            argv_push(&a, "-c");
            argv_push(&a, t->sources.data[i]);
            argv_push(&a, "-o");
            argv_push(&a, ofile);
            push_compile_flags(&a, ctx, t, &owned);

            // Level 2: content-hash CAS lookup — restore from cache if key matches.
            uint64_t ckey = 0;
            if (ctx->cache_dir) {
                ckey = source_cache_key(t->sources.data[i], (char *const *)a.data);
                if (cache_lookup(ctx->cache_dir, ckey, ofile, ".o")) {
                    if (!ctx->quiet || ctx->verbose)
                        printf("[%d/%d] (cached) %s\n", ++(*step), total, t->sources.data[i]);
                    else
                        ++(*step);
                    free(a.data);
                    strarray_push(objs, xstrdup(ofile));
                    continue;
                }
            }

            if (!ctx->quiet || ctx->verbose) {
                print_cmd(++(*step), total, (char *const *)a.data);
                fflush(stdout);
            } else {
                ++(*step);
            }

            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "build: fork failed: %s\n", strerror(errno));
                free(a.data);
                any_failed = 1;
                continue;
            }
            if (pid == 0) {
                execvp(a.data[0], (char *const *)a.data);
                _exit(127);
            }
            free(a.data);

            for (int j = 0; j < jobs; j++) {
                if (pool[j].pid == 0) {
                    pool[j].pid = pid;
                    pool[j].ofile = xstrdup(ofile);
                    pool[j].cache_key = ckey;
                    in_flight++;
                    break;
                }
            }
        }

        free(pool);
        free_strarray(&owned);
        return any_failed ? 1 : 0;
    }
serial_fallback:;
#endif

    // Serial path (jobs==1, dry-run, or non-POSIX).
    int rc = 0;
    for (int i = 0; i < t->sources.len && rc == 0; i++) {
        if (source_is_excluded(t, t->sources.data[i]))
            continue;
        char *stem = stem_of(t->sources.data[i]);
        char ofile[1024];
        snprintf(ofile, sizeof(ofile), "%s/%s.o", objdir, stem);
        free(stem);

        // Level 1: mtime check — skip recompile when ofile is up to date.
        if (ctx->cache_dir && !ctx->dry_run && arch_ok &&
            ofile_is_current(ofile, t->sources.data[i])) {
            if (!ctx->quiet || ctx->verbose)
                printf("[%d/%d] (cached) %s\n", ++(*step), total, t->sources.data[i]);
            else
                ++(*step);
            strarray_push(objs, xstrdup(ofile));
            continue;
        }

        ArgVec a = {0};
        argv_push(&a, cc);
        argv_push(&a, "-c");
        argv_push(&a, t->sources.data[i]);
        argv_push(&a, "-o");
        argv_push(&a, ofile);
        push_compile_flags(&a, ctx, t, &owned);

        // Level 2: content-hash CAS lookup (skipped in dry-run).
        uint64_t ckey = 0;
        if (ctx->cache_dir && !ctx->dry_run) {
            ckey = source_cache_key(t->sources.data[i], (char *const *)a.data);
            if (cache_lookup(ctx->cache_dir, ckey, ofile, ".o")) {
                if (!ctx->quiet || ctx->verbose)
                    printf("[%d/%d] (cached) %s\n", ++(*step), total, t->sources.data[i]);
                else
                    ++(*step);
                free(a.data);
                strarray_push(objs, xstrdup(ofile));
                continue;
            }
        }

        rc = run_step(ctx, ++(*step), total, &a, t);
        free(a.data);
        if (rc == 0 && ctx->cache_dir)
            cache_store(ctx->cache_dir, ckey, ofile, ".o");
        strarray_push(objs, xstrdup(ofile));
    }
    free_strarray(&owned);
    return rc;
}

// ============================================================================
// kind=bytecode LinkWith support (#563)
// ============================================================================

// Seen-set helpers used by collect_bytecode_inputs to dedup target pointers and
// source path strings.

typedef struct { BuildTarget **data; int len, cap; } TargetSet;
typedef struct { const char **data; int len, cap; } StrSet;

static int tset_has(TargetSet *s, BuildTarget *t) {
    for (int i = 0; i < s->len; i++) if (s->data[i] == t) return 1;
    return 0;
}
static void tset_add(TargetSet *s, BuildTarget *t) {
    if (s->len == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->data = realloc(s->data, sizeof(*s->data) * (size_t)s->cap);
    }
    s->data[s->len++] = t;
}
static int sset_has(StrSet *s, const char *str) {
    for (int i = 0; i < s->len; i++) if (strcmp(s->data[i], str) == 0) return 1;
    return 0;
}
static void sset_add(StrSet *s, const char *str) {
    if (s->len == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->data = realloc(s->data, sizeof(*s->data) * (size_t)s->cap);
    }
    s->data[s->len++] = str;
}

// Recursively collect sources, includes, defines, and undefs from `t` and its
// transitive LinkWith deps into the output StringArrays.  Target pointers are
// deduped via `seen_tgts` (diamond deps); source paths are deduped via
// `seen_srcs` to prevent duplicate-symbol errors in cc_link_progs.
// All strings pushed are aliases of the originals (not copied); callers must
// not free them independently.
// skip_c4a: if non-zero, do not recurse into bytecode static lib (.c4a) deps —
// those are linked via --link in bytecode EXE builds (#565).
static void collect_bytecode_inputs(BuildTarget *t,
                                     StringArray *srcs,
                                     StringArray *incs,
                                     StringArray *defs,
                                     StringArray *undefs,
                                     TargetSet *seen_tgts,
                                     StrSet    *seen_srcs,
                                     int skip_c4a) {
    if (tset_has(seen_tgts, t)) return;
    tset_add(seen_tgts, t);

    // Sources (respecting this target's excludes).
    for (int i = 0; i < t->sources.len; i++) {
        const char *src = t->sources.data[i];
        if (source_is_excluded(t, src)) continue;
        if (!sset_has(seen_srcs, src)) {
            sset_add(seen_srcs, src);
            strarray_push(srcs, t->sources.data[i]);
        }
    }
    // Per-target includes, defines, undefs.
    for (int i = 0; i < t->includes.len; i++) strarray_push(incs,   t->includes.data[i]);
    for (int i = 0; i < t->defines.len;  i++) strarray_push(defs,   t->defines.data[i]);
    for (int i = 0; i < t->undefs.len;   i++) strarray_push(undefs, t->undefs.data[i]);

    // Recurse into LinkWith deps (DependsOn are ordering-only, not folded).
    for (int i = 0; i < t->deps_count; i++) {
        if (!t->deps_link[i]) continue; // DependsOn: skip
        BuildTarget *dep = t->deps[i];
        if (dep->sources.len == 0) continue; // source-less dep: warned elsewhere
        // For bytecode EXE targets, skip .c4a deps — they are built standalone
        // and linked via --link (#565).
        if (skip_c4a && dep->kind == CCCC_TGT_BYTECODE && dep->bytecode_subkind == 1)
            continue;
        collect_bytecode_inputs(dep, srcs, incs, defs, undefs, seen_tgts, seen_srcs, skip_c4a);
    }
}

// Mark transitive LinkWith deps of a non-exe kind=bytecode target as folded.
// A folded target's sources are compiled into the dependent's single cccc
// invocation rather than as a standalone artifact.
// Exclusions (never folded):
//   - Dynamic bytecode libs (.c4d, subkind==2): built standalone; loaded at
//     runtime via cc_load_module().
//   - Static bytecode libs (.c4a, subkind==1): built standalone when they are
//     a direct dep of a bytecode EXE — those use --link instead (#565).
//     Static libs that are deps of OTHER static libs are still folded here.
static void mark_bytecode_folded_deps(BuildTarget *dep, int for_exe) {
    if (dep->bytecode_folded) return; // already visited
    // Dynamic bytecode libs are always built standalone.
    if (dep->kind == CCCC_TGT_BYTECODE && dep->bytecode_subkind == 2) return;
    // Static bytecode libs that are direct deps of a bytecode EXE are built
    // standalone and linked via --link (#565).
    if (for_exe && dep->kind == CCCC_TGT_BYTECODE && dep->bytecode_subkind == 1) return;
    dep->bytecode_folded = 1;
    for (int i = 0; i < dep->deps_count; i++) {
        if (dep->deps_link[i])
            // Recursive deps of a static lib are still folded (not for_exe).
            mark_bytecode_folded_deps(dep->deps[i], 0);
    }
}

// Build a single target (its sources already; deps assumed built).  Returns 0 ok.
// `cc` is the global fallback CC; per-target and cross_cc overrides are applied
// inside this function via effective_cc_for_target.
static int build_target(Builder *ctx, const char *cc,
                        BuildTarget *t, int *step, int total) {
    // Bytecode-library targets are folded into their bytecode dependent's single
    // cccc invocation and must not be built standalone (#563).
    if (t->bytecode_folded)
        return 0;

    if (ctx->verbose) {
        const char *kind_str = t->kind == CCCC_TGT_EXE      ? "executable"
                             : t->kind == CCCC_TGT_STATIC    ? "static library"
                             : t->kind == CCCC_TGT_DYNAMIC   ? "dynamic library"
                             : t->kind == CCCC_TGT_BYTECODE  ? (t->bytecode_subkind == 1 ? "bytecode-static"
                                                               : t->bytecode_subkind == 2 ? "bytecode-dynamic"
                                                               :                            "bytecode")
                             :                                  "custom";
        if (t->kind == CCCC_TGT_CUSTOM)
            printf(">> target '%s' [%s]\n", t->name, kind_str);
        else
            printf(">> target '%s' [%s, %d source(s)]\n",
                   t->name, kind_str, t->sources.len);
        fflush(stdout);
    }

    // Custom steps: run the shell command and return its exit code.
    if (t->kind == CCCC_TGT_CUSTOM) {
        if (t->command && *t->command) {
            printf("[%d/%d] (custom) %s\n", ++(*step), total, t->command);
            fflush(stdout);
            if (ctx->dry_run)
                return 0;
#ifdef _POSIX_VERSION
            // Build the shell context with the tool allowlist applied.
            shell_ctx *sctx = shell_ctx_create();
            if (!sctx) {
                fprintf(stderr, "build: RunCustom '%s': failed to create shell context\n",
                        t->name);
                return 1;
            }
            for (int i = 0; i < ctx->tool_allow_count; i++)
                shell_ctx_allowlist_cmd(sctx, ctx->tool_allow[i]);
            // Both callbacks must be set: without them the parent drain loop
            // falls through to ensure_buffer_capacity/xmalloc in posix_shell_with_io,
            // which calls die() in the parent process on OOM.
            shell_io sio = { .out_cb = build_passthru_out, .err_cb = build_passthru_err };
            int rc = shell_with_ctx(t->command, &sio, sctx);
            shell_ctx_destroy(sctx);
            if (rc != 0) {
                fprintf(stderr, "build: custom step '%s' failed (exit %d)\n",
                        t->name, rc);
            }
            return rc;
#else
            fprintf(stderr, "build: RunCustom not supported on this platform\n");
            return 1;
#endif
        }
        return 0;
    }

    char *out_rel = t->output ? xstrdup(t->output) : default_output(t);
    char *out_abs = join(ctx->out_dir, out_rel);
    char *out_dir = dir_of(out_abs);
    char *objdir = join(ctx->out_dir, "obj");
    char *tobjdir = join(objdir, t->name);
    free(objdir);
    if (!ctx->dry_run && (mkdir_p(out_dir) != 0 || mkdir_p(tobjdir) != 0)) {
        fprintf(stderr, "build: failed to create output directories\n");
        free(out_rel); free(out_abs); free(out_dir); free(tobjdir);
        return 1;
    }

    // Bytecode target (#545 + #563 + #564): single whole-program cccc invocation.
    // exe (bytecode_subkind=0)     → bin/<name>.c4   (runs cccc without -c; main required)
    //   LinkWith .c4a deps are built standalone and linked via --link (#565).
    // static lib (subkind=1)       → lib/<name>.c4a  (cccc -c bytecode; no main required)
    //   LinkWith deps are source-folded into the .c4a (self-contained snapshot).
    // dynamic lib (subkind=2)      → lib/<name>.c4d  (cccc -c bytecode; no main required)
    //   LinkWith deps are source-folded into the .c4d (self-contained module).
    // Source-less LinkWith deps (CUSTOM targets, native FFI libs) are skipped with
    // a warning — linking native artifacts into bytecode requires FFI, not LinkWith.
    if (t->kind == CCCC_TGT_BYTECODE) {
        char *cccc_bin = ctx->cccc_self
                       ? xstrdup(ctx->cccc_self)
                       : cccc_path_find_executable("cccc");
        if (!cccc_bin) {
            fprintf(stderr, "build: cannot find cccc binary for bytecode target '%s'\n",
                    t->name);
            free(out_rel); free(out_abs); free(out_dir); free(tobjdir);
            return 1;
        }

        // Warn about source-less LinkWith deps (out of scope for bytecode merge).
        for (int i = 0; i < t->deps_count; i++) {
            if (t->deps_link[i] && t->deps[i]->sources.len == 0) {
                fprintf(stderr,
                    "build: warning: bytecode target '%s': LinkWith dep '%s' has no "
                    "sources — native FFI linking is out of scope for bytecode targets; "
                    "dep ignored\n",
                    t->name, t->deps[i]->name);
            }
        }

        int is_exe = (t->bytecode_subkind == 0);

        // Collect all sources (transitively, deduped) from this target and its
        // folded deps.  For bytecode EXEs, .c4a deps are skipped (they are built
        // standalone and linked via --link, #565); all other folded deps are
        // collected as before.
        StringArray agg_srcs   = {0};
        StringArray dummy_incs  = {0};
        StringArray dummy_defs  = {0};
        StringArray dummy_undefs = {0};
        TargetSet seen_tgts = {0};
        StrSet    seen_srcs = {0};
        collect_bytecode_inputs(t, &agg_srcs, &dummy_incs, &dummy_defs, &dummy_undefs,
                                &seen_tgts, &seen_srcs, is_exe);
        free(seen_tgts.data);
        free(seen_srcs.data);
        free(dummy_incs.data);
        free(dummy_defs.data);
        free(dummy_undefs.data);

        StringArray owned = {0};
        ArgVec a = {0};
        argv_push(&a, cccc_bin);
        for (int i = 0; i < agg_srcs.len; i++)
            argv_push(&a, agg_srcs.data[i]);
        // Library targets (static .c4a / dynamic .c4d) need --compile=bytecode so
        // that cccc compiles without requiring main() and exits after writing
        // bytecode.  Use the long-form `--compile=bytecode` (not `-c bytecode`)
        // because -c uses optional_argument and the space-separated form would
        // treat `bytecode` as a source file instead of the format specifier.
        if (!is_exe) {
            argv_push(&a, "--compile=bytecode");
        }
        argv_push(&a, "-o");
        argv_push(&a, out_abs);

        // Flags: ctx defaults (--std, -I, -D, -U) + root target's own.
        push_compile_flags_bytecode(&a, ctx, t, &owned);

        // Additionally push per-target includes/defines/undefs from deps.
        // For EXEs, only push -I flags (not sources) from .c4a deps so that
        // the exe can include the lib's headers (#565).
        // For .c4a/.c4d, also push folded dep flags as before.
        TargetSet dep_seen = {0};
        tset_add(&dep_seen, t); // mark root so collect skips it
        for (int i = 0; i < t->deps_count; i++) {
            if (!t->deps_link[i]) continue;
            BuildTarget *dep = t->deps[i];
            if (dep->sources.len == 0) continue;
            if (is_exe && dep->kind == CCCC_TGT_BYTECODE && dep->bytecode_subkind == 1) {
                // .c4a dep of a bytecode EXE: add --link <dep.c4a> flag (#565).
                char *dep_out_rel = default_output(dep);
                char *dep_out_abs = join(ctx->out_dir, dep_out_rel);
                free(dep_out_rel);
                strarray_push(&owned, dep_out_abs); // owned for later free
                argv_push(&a, "--link");
                argv_push(&a, dep_out_abs);
                // Still collect includes from this dep so the exe can find headers.
                StringArray d_srcs = {0}, d_incs = {0}, d_defs = {0}, d_undefs = {0};
                StrSet d_ss = {0};
                TargetSet d_ts = {0};
                collect_bytecode_inputs(dep, &d_srcs, &d_incs, &d_defs, &d_undefs,
                                        &d_ts, &d_ss, 0);
                free(d_ss.data); free(d_ts.data); free(d_srcs.data);
                for (int j = 0; j < d_incs.len; j++) {
                    argv_push(&a, "-I");
                    argv_push(&a, d_incs.data[j]);
                }
                free(d_incs.data); free(d_defs.data); free(d_undefs.data);
                continue;
            }
            if (tset_has(&dep_seen, dep)) continue;
            // Gather this dep's (and its transitive deps') per-target flags.
            StringArray d_srcs = {0}, d_incs = {0}, d_defs = {0}, d_undefs = {0};
            StrSet d_ss = {0};
            collect_bytecode_inputs(dep, &d_srcs, &d_incs, &d_defs, &d_undefs,
                                    &dep_seen, &d_ss, 0);
            free(d_ss.data);
            free(d_srcs.data);
            for (int j = 0; j < d_incs.len; j++) {
                argv_push(&a, "-I");
                argv_push(&a, d_incs.data[j]);
            }
            for (int j = 0; j < d_defs.len; j++) {
                char *f = malloc(strlen(d_defs.data[j]) + 3);
                snprintf(f, strlen(d_defs.data[j]) + 3, "-D%s", d_defs.data[j]);
                strarray_push(&owned, f);
                argv_push(&a, f);
            }
            for (int j = 0; j < d_undefs.len; j++) {
                char *f = malloc(strlen(d_undefs.data[j]) + 3);
                snprintf(f, strlen(d_undefs.data[j]) + 3, "-U%s", d_undefs.data[j]);
                strarray_push(&owned, f);
                argv_push(&a, f);
            }
            free(d_incs.data);
            free(d_defs.data);
            free(d_undefs.data);
        }
        free(dep_seen.data);

        // Per-target incremental cache for bytecode (#562).
        // Level 1: mtime fast path — skip if output is newer than all sources.
        // Level 2: content-hash CAS — restore from cache if key matches.
        const char *bc_ext = t->bytecode_subkind == 1 ? ".c4a"
                           : t->bytecode_subkind == 2 ? ".c4d" : ".c4";
        uint64_t bc_key = 0;
        int brc = 0;

        if (ctx->cache_dir && !ctx->dry_run) {
            if (bytecode_output_is_current(out_abs, &agg_srcs)) {
                if (!ctx->quiet || ctx->verbose)
                    printf("[%d/%d] (cached) %s\n", ++(*step), total, t->name);
                else
                    ++(*step);
                goto bytecode_done;
            }
            bc_key = bytecode_target_cache_key((char *const *)a.data, &agg_srcs);
            if (cache_lookup(ctx->cache_dir, bc_key, out_abs, bc_ext)) {
                if (!ctx->quiet || ctx->verbose)
                    printf("[%d/%d] (cached) %s\n", ++(*step), total, t->name);
                else
                    ++(*step);
                goto bytecode_done;
            }
        }

        brc = run_step(ctx, ++(*step), total, &a, t);
        if (brc == 0 && ctx->cache_dir && !ctx->dry_run && bc_key)
            cache_store(ctx->cache_dir, bc_key, out_abs, bc_ext);

    bytecode_done:
        free(a.data);
        free_strarray(&owned);
        free(agg_srcs.data);
        free(cccc_bin);
        free(out_rel); free(out_abs); free(out_dir); free(tobjdir);
        return brc;
    }

    // Resolve the effective compiler for this target (#547).
    char *eff_cc = effective_cc_for_target(ctx, t);
    if (!eff_cc) {
        fprintf(stderr, "build: could not find a C compiler for target '%s'\n", t->name);
        free(out_rel); free(out_abs); free(out_dir); free(tobjdir);
        return 1;
    }

    StringArray objs = {0};
    int rc = compile_sources(ctx, eff_cc, t, tobjdir, &objs, step, total);
    if (rc != 0) {
        goto done;
    }

    if (t->kind == CCCC_TGT_STATIC) {
        char *ar = cccc_find_native_tool("ar");
        if (!ar) { rc = 1; goto done; }
        ArgVec a = {0};
        argv_push(&a, ar);
        argv_push(&a, "rcs");
        argv_push(&a, out_abs);
        for (int i = 0; i < objs.len; i++)
            argv_push(&a, objs.data[i]);
        rc = run_step(ctx, ++(*step), total, &a, t);
        free(a.data);
        free(ar);
    } else {
        // Executable or dynamic library: link with effective cc.
        ArgVec a = {0};
        StringArray owned = {0};
        argv_push(&a, eff_cc);
        if (t->kind == CCCC_TGT_DYNAMIC)
            argv_push(&a, "-shared");
        push_cross_flags(&a, ctx, t, &owned); // --target=<triple>
        argv_push(&a, "-o");
        argv_push(&a, out_abs);
        for (int i = 0; i < objs.len; i++)
            argv_push(&a, objs.data[i]);
        // Link against dependency libraries via -L<out>/lib -l<dep>.
        // Only deps created with LinkWith (deps_link[i]==1) contribute -l flags.
        char *libdir = join(ctx->out_dir, "lib");
        int have_libdir = 0;
        for (int i = 0; i < t->deps_count; i++) {
            if (!t->deps_link[i])
                continue; // DependsOn edge — ordering only, no linker flag
            if (!have_libdir) {
                argv_push(&a, "-L");
                argv_push(&a, libdir);
                have_libdir = 1;
            }
            char flag[256];
            snprintf(flag, sizeof(flag), "-l%s", t->deps[i]->name);
            // flag is stack — push a stable copy
            char *f = xstrdup(flag);
            strarray_push(&objs, f); // reuse objs as an owner pool until done
            argv_push(&a, f);
        }
        for (int i = 0; i < t->libpaths.len; i++) {
            argv_push(&a, "-L");
            argv_push(&a, t->libpaths.data[i]);
        }
        for (int i = 0; i < t->libs.len; i++) {
            char *f = malloc(strlen(t->libs.data[i]) + 3);
            snprintf(f, strlen(t->libs.data[i]) + 3, "-l%s", t->libs.data[i]);
            strarray_push(&owned, f);
            argv_push(&a, f);
        }
        for (int i = 0; i < t->ldflags.len; i++)
            argv_push(&a, t->ldflags.data[i]);
        rc = run_step(ctx, ++(*step), total, &a, t);
        free(a.data);
        free_strarray(&owned);
        free(libdir);
    }

done:
    free(eff_cc);
    free_strarray(&objs);
    free(out_rel); free(out_abs); free(out_dir); free(tobjdir);
    return rc;
}

// Count the total number of toolchain steps for the selected target set.
static int count_steps(BuildTarget **order, int n) {
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (order[i]->bytecode_folded)
            continue; // skipped — folded into a bytecode dependent (#563)
        if (order[i]->kind == CCCC_TGT_CUSTOM)
            total += 1; // one step: run the custom command
        else if (order[i]->kind == CCCC_TGT_BYTECODE)
            total += 1; // one step: single cccc whole-program invocation (#545)
        else
            total += order[i]->sources.len + 1; // one compile per source + 1 link/ar
    }
    return total;
}

// Depth-first topological visit; appends to `order`.  Returns 0 ok, -1 cycle.
static int topo_visit(BuildTarget *t, BuildTarget **order, int *n) {
    if (t->visited == 2)
        return 0;
    if (t->visited == 1) {
        fprintf(stderr, "build: dependency cycle detected at target '%s'\n", t->name);
        return -1;
    }
    t->visited = 1;
    for (int i = 0; i < t->deps_count; i++)
        if (topo_visit(t->deps[i], order, n) != 0)
            return -1;
    t->visited = 2;
    order[(*n)++] = t;
    return 0;
}

// Returns 1 if t is ready to build: PENDING with all deps DONE. (#557)
static int target_is_ready(BuildTarget *t) {
    if (t->state != TARGET_PENDING) return 0;
    for (int i = 0; i < t->deps_count; i++)
        if (t->deps[i]->state != TARGET_DONE) return 0;
    return 1;
}

// Fixpoint pass: mark PENDING targets whose dep chain includes a FAILED or
// SKIPPED target as SKIPPED so they are never attempted. (#557)
static void propagate_skipped(BuildTarget **order, int n) {
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < n; i++) {
            if (order[i]->state != TARGET_PENDING) continue;
            for (int j = 0; j < order[i]->deps_count; j++) {
                int ds = order[i]->deps[j]->state;
                if (ds == TARGET_FAILED || ds == TARGET_SKIPPED) {
                    order[i]->state = TARGET_SKIPPED;
                    changed = 1;
                    break;
                }
            }
        }
    }
}

// Look up a registered target by name; returns NULL if not found.
static BuildTarget *find_target_by_name(Builder *ctx, const char *name) {
    for (int i = 0; i < ctx->targets_count; i++)
        if (strcmp(ctx->targets[i]->name, name) == 0)
            return ctx->targets[i];
    return NULL;
}

// Run the build for an explicit target set (NULL = all targets).  Returns the
// number of failed targets (0 = success).
static int run_graph(Builder *ctx, BuildTarget *only) {
    ctx->run_invoked = 1;

    // --build-target=NAME: if the CLI filter is set, resolve it now (after the
    // entry has populated the full target graph) and override `only`.  A CLI
    // selection beats an explicit cccc_build_run(x) argument (zig build <step>
    // model).
    if (ctx->target_filter) {
        BuildTarget *sel = find_target_by_name(ctx, ctx->target_filter);
        if (!sel) {
            fprintf(stderr, "build: --build-target '%s' does not match any declared target\n",
                    ctx->target_filter);
            if (ctx->targets_count > 0) {
                fprintf(stderr, "  available targets:");
                for (int i = 0; i < ctx->targets_count; i++)
                    fprintf(stderr, " %s", ctx->targets[i]->name);
                fprintf(stderr, "\n");
            }
            ctx->failed = 1;
            return 1;
        }
        only = sel;
    }

    if (ctx->targets_count == 0) {
        printf("build: no targets declared\n");
        return 0;
    }
    // Reset topo markers and bytecode_folded, then mark transitive LinkWith deps
    // of kind=bytecode targets as folded libraries (#563).
    for (int i = 0; i < ctx->targets_count; i++) {
        ctx->targets[i]->visited = 0;
        ctx->targets[i]->bytecode_folded = 0;
        ctx->targets[i]->state = TARGET_PENDING;
    }
    for (int i = 0; i < ctx->targets_count; i++) {
        if (ctx->targets[i]->kind != CCCC_TGT_BYTECODE) continue;
        int is_exe = (ctx->targets[i]->bytecode_subkind == 0);
        for (int j = 0; j < ctx->targets[i]->deps_count; j++) {
            if (ctx->targets[i]->deps_link[j])
                // EXE targets do not fold .c4a deps — they use --link (#565).
                mark_bytecode_folded_deps(ctx->targets[i]->deps[j], is_exe);
        }
    }

    BuildTarget **order = calloc(ctx->targets_count, sizeof(*order));
    int n = 0;
    int cyc = 0;
    if (only) {
        cyc = topo_visit(only, order, &n);
    } else {
        for (int i = 0; i < ctx->targets_count && cyc == 0; i++)
            cyc = topo_visit(ctx->targets[i], order, &n);
    }
    if (cyc != 0) {
        free(order);
        ctx->failed = 1;
        return 1;
    }

    char *cc = cccc_find_native_cc();
    if (!cc) {
        free(order);
        ctx->failed = 1;
        return 1;
    }

    // Pre-mark bytecode-folded targets as DONE so their dependents become ready
    // immediately; mirrors the early-return in build_target() (#557).
    for (int i = 0; i < n; i++)
        if (order[i]->bytecode_folded)
            order[i]->state = TARGET_DONE;

    int total = count_steps(order, n);
    int step = 0;
    int failures = 0;
    const char **failed_names = calloc(n, sizeof(*failed_names));

#ifdef _POSIX_VERSION
    if (ctx->jobs > 1 && !ctx->dry_run) {
        // Target-level parallel dispatch: fork up to `jobs` child processes for
        // simultaneously-ready targets.  The -j budget is shared: each forked
        // child runs source compilation serially (jobs=1) so the total number of
        // concurrent compiler invocations never exceeds `jobs` (#557).
        typedef struct { pid_t pid; BuildTarget *t; } TJob;
        TJob *inflight_jobs = calloc(ctx->jobs, sizeof(TJob));
        int in_flight = 0, stop_dispatch = 0;

        for (;;) {
            // Exit when no targets remain pending or in-flight.
            int remaining = 0;
            for (int i = 0; i < n; i++)
                if (order[i]->state == TARGET_PENDING ||
                    order[i]->state == TARGET_INFLIGHT)
                    remaining++;
            if (remaining == 0) break;

            // Count how many targets are currently ready to build.
            int ready_count = 0;
            for (int i = 0; i < n; i++)
                if (target_is_ready(order[i])) ready_count++;

            // Lone ready target with nothing in-flight: run in-process so it
            // can use the full source-level -j parallelism inside compile_sources().
            // Running in-process while in_flight>0 would cause waitpid(-1) races
            // between compile_sources() and the parent's reap loop below.
            if (!stop_dispatch && ready_count == 1 && in_flight == 0) {
                for (int i = 0; i < n; i++) {
                    if (!target_is_ready(order[i])) continue;
                    if (build_target(ctx, cc, order[i], &step, total) != 0) {
                        fprintf(stderr, "build: target '%s' failed%s\n",
                                order[i]->name,
                                ctx->keep_going ? ", continuing" : "");
                        order[i]->state = TARGET_FAILED;
                        if (failed_names) failed_names[failures] = order[i]->name;
                        failures++;
                        if (!ctx->keep_going) { stop_dispatch = 1; break; }
                        propagate_skipped(order, n);
                    } else {
                        order[i]->state = TARGET_DONE;
                    }
                    break;
                }
                continue;
            }

            // Dispatch ready targets as forked children up to the job limit.
            if (!stop_dispatch) {
                for (int i = 0; i < n && in_flight < ctx->jobs; i++) {
                    if (!target_is_ready(order[i])) continue;
                    fflush(stdout); fflush(stderr);
                    pid_t pid = fork();
                    if (pid < 0) {
                        fprintf(stderr, "build: fork failed: %s\n", strerror(errno));
                        order[i]->state = TARGET_FAILED;
                        if (failed_names) failed_names[failures] = order[i]->name;
                        failures++;
                        continue;
                    }
                    if (pid == 0) {
                        // Child: compile sources serially to respect shared -j budget.
                        ctx->jobs = 1;
                        int local_step = 0;
                        int local_total = count_steps(&order[i], 1);
                        int rc = build_target(ctx, cc, order[i], &local_step, local_total);
                        fflush(stdout); fflush(stderr);
                        _exit(rc == 0 ? 0 : 1);
                    }
                    order[i]->state = TARGET_INFLIGHT;
                    for (int j = 0; j < ctx->jobs; j++) {
                        if (inflight_jobs[j].pid == 0) {
                            inflight_jobs[j].pid = pid;
                            inflight_jobs[j].t = order[i];
                            in_flight++;
                            break;
                        }
                    }
                }
            }

            // If nothing is running and nothing was dispatched, all remaining
            // PENDING targets are blocked by failed deps — mark them skipped.
            if (in_flight == 0) {
                for (int i = 0; i < n; i++)
                    if (order[i]->state == TARGET_PENDING)
                        order[i]->state = TARGET_SKIPPED;
                break;
            }

            // Reap one child.
            int status;
            pid_t done = waitpid(-1, &status, 0);
            if (done < 0) break;
            for (int j = 0; j < ctx->jobs; j++) {
                if (inflight_jobs[j].pid != done) continue;
                BuildTarget *t = inflight_jobs[j].t;
                inflight_jobs[j].pid = 0;
                inflight_jobs[j].t = NULL;
                in_flight--;
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    t->state = TARGET_DONE;
                } else {
                    fprintf(stderr, "build: target '%s' failed%s\n", t->name,
                            ctx->keep_going ? ", continuing" : "");
                    t->state = TARGET_FAILED;
                    if (failed_names) failed_names[failures] = t->name;
                    failures++;
                    if (!ctx->keep_going) {
                        stop_dispatch = 1;
                        // Drain remaining in-flight children before returning.
                        while (in_flight > 0) {
                            pid_t d2 = waitpid(-1, &status, 0);
                            if (d2 < 0) break;
                            for (int k = 0; k < ctx->jobs; k++) {
                                if (inflight_jobs[k].pid != d2) continue;
                                inflight_jobs[k].pid = 0;
                                inflight_jobs[k].t = NULL;
                                in_flight--;
                                break;
                            }
                        }
                    } else {
                        propagate_skipped(order, n);
                    }
                }
                break;
            }
        }

        free(inflight_jobs);
    } else
#endif
    {
        // Serial path: jobs==1, dry-run, or non-POSIX.
        for (int i = 0; i < n; i++) {
            if (build_target(ctx, cc, order[i], &step, total) != 0) {
                fprintf(stderr, "build: target '%s' failed%s\n", order[i]->name,
                        ctx->keep_going ? ", continuing" : "");
                if (failed_names)
                    failed_names[failures] = order[i]->name;
                failures++;
                if (!ctx->keep_going)
                    break;
            }
        }
    }

    free(cc);

    if (failures == 0) {
        printf("build succeeded (%d target%s, 0 errors)\n", n, n == 1 ? "" : "s");
    } else {
        printf("build failed (%d error%s)\n", failures, failures == 1 ? "" : "s");
        if (ctx->keep_going && failed_names) {
            for (int i = 0; i < failures; i++)
                printf("  failed: %s\n", failed_names[i]);
            // Report targets skipped because a dependency failed.
            for (int i = 0; i < n; i++)
                if (order[i]->state == TARGET_SKIPPED)
                    printf("  skipped: %s\n", order[i]->name);
        }
        ctx->failed = 1;
    }
    free(order);
    free(failed_names);
    return failures;
}

static long long impl_build_run(long long ctx, long long t) {
    (void)ctx;
    if (!s_ctx)
        return 1;
    return run_graph(s_ctx, (BuildTarget *)(intptr_t)t);
}
static long long impl_build_run_all(long long ctx) {
    (void)ctx;
    if (!s_ctx)
        return 1;
    return run_graph(s_ctx, NULL);
}
static long long impl_build_run_default(long long ctx) {
    return impl_build_run_all(ctx);
}

// ============================================================================
// Entry resolution + driver
// ============================================================================

static Obj *find_fn(Obj *prog, const char *name) {
    for (Obj *o = prog; o; o = o->next)
        if (o->is_function && o->name && strcmp(o->name, name) == 0)
            return o;
    return NULL;
}

// Resolve the build entry name: explicit flag, else a single [[cccc::build]],
// else "build_main".  Returns NULL and prints a diagnostic on ambiguity.
static const char *resolve_entry(VirtualMachine *vm, const char *flag) {
    if (flag)
        return flag;
    BuildFnRecord *list = vm->compiler.build_fns;
    if (list) {
        if (list->next) {
            fprintf(stderr, "build: multiple [[cccc::build]] entries found:\n");
            for (BuildFnRecord *b = list; b; b = b->next)
                fprintf(stderr, "  %s\n", b->name);
            fprintf(stderr, "  use --build-entry=NAME to disambiguate\n");
            return NULL;
        }
        return list->name;
    }
    return "build_main";
}

static void free_target(BuildTarget *t) {
    free(t->name);
    free(t->output);
    free(t->command);
    free(t->profile);
    free(t->cc_override);
    free(t->target_triple);
    free_strarray(&t->sources);
    free_strarray(&t->excludes);
    free_strarray(&t->includes);
    free_strarray(&t->defines);
    free_strarray(&t->undefs);
    free_strarray(&t->cflags);
    free_strarray(&t->ldflags);
    free_strarray(&t->libs);
    free_strarray(&t->libpaths);
    free(t->deps);
    free(t->deps_link);
    free(t);
}

// run_install: copy all registered install_targets to install_prefix/{bin,lib}.
// Respects dry_run.  Returns 0 on success, non-zero if any copy fails.
static int run_install(Builder *ctx) {
    int failed = 0;
    for (int i = 0; i < ctx->install_count; i++) {
        BuildTarget *t = ctx->install_targets[i];
        // Determine destination subdirectory and filename within prefix.
        char dest_rel[512];
        switch (t->kind) {
        case CCCC_TGT_EXE:
            snprintf(dest_rel, sizeof(dest_rel), "bin/%s", t->name);
            break;
        case CCCC_TGT_STATIC:
            snprintf(dest_rel, sizeof(dest_rel), "lib/lib%s.a", t->name);
            break;
        case CCCC_TGT_DYNAMIC:
            snprintf(dest_rel, sizeof(dest_rel), "lib/lib%s.%s", t->name, CCCC_DYLIB_EXT);
            break;
        case CCCC_TGT_BYTECODE:
            if (t->bytecode_subkind == 1)
                snprintf(dest_rel, sizeof(dest_rel), "lib/%s.c4a", t->name);
            else if (t->bytecode_subkind == 2)
                snprintf(dest_rel, sizeof(dest_rel), "lib/%s.c4d", t->name);
            else
                snprintf(dest_rel, sizeof(dest_rel), "bin/%s.c4", t->name);
            break;
        default:
            fprintf(stderr, "build: install: cannot install target '%s' (unsupported kind)\n",
                    t->name);
            failed++;
            continue;
        }
        // Source: the built artifact path.
        char *src_rel = t->output ? xstrdup(t->output) : default_output(t);
        char *src = join(ctx->out_dir, src_rel);
        free(src_rel);
        // Destination: prefix + dest_rel.
        char *dst = join(ctx->install_prefix, dest_rel);
        char *dst_dir = dir_of(dst);

        if (ctx->dry_run) {
            printf("install %s -> %s\n", src, dst);
            free(src); free(dst); free(dst_dir);
            continue;
        }
        if (mkdir_p(dst_dir) != 0) {
            fprintf(stderr, "build: install: failed to create directory %s\n", dst_dir);
            free(src); free(dst); free(dst_dir);
            failed++;
            continue;
        }
        free(dst_dir);

        // Copy file contents.
        FILE *in = fopen(src, "rb");
        if (!in) {
            fprintf(stderr, "build: install: cannot open %s: %s\n", src, strerror(errno));
            free(src); free(dst);
            failed++;
            continue;
        }
        FILE *out = fopen(dst, "wb");
        if (!out) {
            fprintf(stderr, "build: install: cannot create %s: %s\n", dst, strerror(errno));
            fclose(in); free(src); free(dst);
            failed++;
            continue;
        }
        char buf[65536];
        size_t n;
        int copy_ok = 1;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            if (fwrite(buf, 1, n, out) != n) { copy_ok = 0; break; }
        }
        fclose(in);
        if (fclose(out) != 0 || !copy_ok) {
            fprintf(stderr, "build: install: write error for %s\n", dst);
            free(src); free(dst);
            failed++;
            continue;
        }
#ifdef _POSIX_VERSION
        // Preserve executable bit for binaries.
        if (t->kind == CCCC_TGT_EXE) chmod(dst, 0755);
#endif
        if (ctx->verbose)
            printf("installed  %s -> %s\n", t->name, dst);
        else
            printf("install    %s\n", dst);
        free(src); free(dst);
    }
    return failed;
}

int cc_run_build(VirtualMachine *vm, Obj *prog, const CcBuildOptions *opts) {
    // Count [[cccc::build_target]] factories and copy their names so they are
    // accessible from FFI impls (impl_target_count / impl_target_name) and for
    // the --build-list-targets and factory-direct --build-target logic below.
    int factory_count = 0;
    for (BuildTargetFnRecord *r = vm->compiler.build_target_fns; r; r = r->next)
        factory_count++;
    const char **factory_names = NULL;
    if (factory_count > 0) {
        factory_names = calloc(factory_count, sizeof(*factory_names));
        int fi = 0;
        for (BuildTargetFnRecord *r = vm->compiler.build_target_fns; r; r = r->next)
            factory_names[fi++] = r->name;
    }

    // --build-list-targets: print factory names and exit without running the entry.
    if (opts->list_targets) {
        if (factory_count == 0) {
            printf("(no [[cccc::build_target]] factories found)\n");
        } else {
            for (int i = 0; i < factory_count; i++)
                printf("%s\n", factory_names[i]);
        }
        free(factory_names);
        return 0;
    }

    // --build-target=NAME: check if NAME matches a [[cccc::build_target]] factory.
    // If so, invoke the factory directly (skipping build_main) and build its target.
    if (opts->target_name) {
        for (int i = 0; i < factory_count; i++) {
            if (strcmp(factory_names[i], opts->target_name) == 0) {
                Obj *factory_fn = find_fn(prog, opts->target_name);
                if (!factory_fn || !factory_fn->is_function) {
                    fprintf(stderr, "build: factory '%s' not found in compiled output\n",
                            opts->target_name);
                    free(factory_names);
                    return 1;
                }

                char cwd[1024];
                if (!getcwd(cwd, sizeof(cwd)))
                    snprintf(cwd, sizeof(cwd), ".");

                Builder ctx = {0};
                ctx.root = cwd;
                ctx.out_dir = xstrdup(opts->out_dir ? opts->out_dir : "build");
                ctx.host = CCCC_BUILD_HOST;
                ctx.verbose = opts->verbose || opts->build_verbose;
                ctx.quiet = opts->quiet && !(opts->verbose || opts->build_verbose);
                ctx.keep_going = opts->keep_going;
                ctx.dry_run = opts->dry_run;
                ctx.jobs = opts->jobs > 1 ? opts->jobs : 1;
                ctx.defaults = opts->defaults;
                ctx.tool_allow = opts->tool_allow;
                ctx.tool_allow_count = opts->tool_allow_count;
                ctx.factory_names = factory_names;
                ctx.factory_count = factory_count;
                ctx.profile = opts->profile;
                ctx.cross_triple = opts->cross_triple;
                ctx.cross_cc = opts->cross_cc;
                ctx.cccc_self = opts->cccc_self;
                if (opts->build_cache) {
                    ctx.cache_dir = *opts->build_cache
                        ? xstrdup(opts->build_cache)
                        : join(ctx.out_dir, ".cccc-cache");
                    mkdir_p(ctx.cache_dir);
                }
                ctx.build_options = opts->build_options;
                ctx.build_options_count = opts->build_options_count;
                ctx.build_install = opts->build_install;
                ctx.user_args       = opts->user_args;
                ctx.user_args_count = opts->user_args_count;
                const char *prefix_env = getenv("PREFIX");
                ctx.install_prefix = xstrdup(prefix_env ? prefix_env : "/usr/local");

                s_ctx = &ctx;
                cc_run_at(vm, (Pc)factory_fn->code_addr, 0, NULL);
                BuildTarget *tgt = (BuildTarget *)(intptr_t)vm->regs[REG_A0];
                s_ctx = NULL;

                // Apply factory-level kind=bytecode override (#545, #564): if the
                // matching factory record declares kind=bytecode, promote the
                // returned target AND all StaticLib/DynamicLib targets created
                // within the factory to CCCC_TGT_BYTECODE with the appropriate
                // bytecode_subkind so the host runner uses the bytecode pipeline.
                // EXE deps are intentionally left as CCCC_TGT_EXE: they are
                // consumed via source-folding, not built standalone.
                bool factory_is_bytecode = false;
                if (tgt) {
                    for (BuildTargetFnRecord *r = vm->compiler.build_target_fns;
                         r; r = r->next) {
                        if (strcmp(r->name, opts->target_name) == 0 &&
                            r->kind && strcmp(r->kind, "bytecode") == 0) {
                            factory_is_bytecode = true;
                            break;
                        }
                    }
                }
                if (factory_is_bytecode) {
                    for (int j = 0; j < ctx.targets_count; j++) {
                        BuildTarget *dep = ctx.targets[j];
                        if (dep->kind == CCCC_TGT_STATIC) {
                            dep->kind = CCCC_TGT_BYTECODE;
                            dep->bytecode_subkind = 1;
                        } else if (dep->kind == CCCC_TGT_DYNAMIC) {
                            dep->kind = CCCC_TGT_BYTECODE;
                            dep->bytecode_subkind = 2;
                        } else if (dep == tgt && dep->kind == CCCC_TGT_EXE) {
                            dep->kind = CCCC_TGT_BYTECODE;
                            dep->bytecode_subkind = 0;
                        }
                    }
                }

                int exit_code = 0;
                if (!tgt) {
                    fprintf(stderr, "build: factory '%s' returned NULL\n",
                            opts->target_name);
                    exit_code = 1;
                } else {
                    exit_code = run_graph(&ctx, tgt) ? 1 : 0;
                }
                if (exit_code == 0 && ctx.build_install && ctx.install_count > 0)
                    if (run_install(&ctx) != 0) exit_code = 1;

                for (int j = 0; j < ctx.targets_count; j++)
                    free_target(ctx.targets[j]);
                free(ctx.targets);
                free(ctx.out_dir);
                free(ctx.cache_dir);
                for (int j = 0; j < ctx.captures_count; j++) free(ctx.captures[j]);
                free(ctx.captures);
                free(ctx.install_prefix);
                free(ctx.install_targets);
                if (ctx.original_cwd) { chdir(ctx.original_cwd); free(ctx.original_cwd); }
                free(factory_names);
                return exit_code;
            }
        }
        // NAME did not match any factory; fall through to entry-based flow where
        // run_graph will match it against registered target names (existing behaviour).
    }

    // Entry-based flow: resolve and invoke build_main (or the --build-entry function),
    // then run_graph filters by --build-target if set.
    const char *entry = resolve_entry(vm, opts->entry_name);
    if (!entry) {
        free(factory_names);
        return 1;
    }

    Obj *fn = find_fn(prog, entry);
    if (!fn || !fn->is_function) {
        fprintf(stderr, "build: entry '%s' not found\n", entry);
        free(factory_names);
        return 1;
    }

    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd)))
        snprintf(cwd, sizeof(cwd), ".");

    Builder ctx = {0};
    ctx.root = cwd;
    ctx.out_dir = xstrdup(opts->out_dir ? opts->out_dir : "build");
    ctx.host = CCCC_BUILD_HOST;
    ctx.target_filter = opts->target_name;
    ctx.verbose = opts->verbose || opts->build_verbose;
    ctx.quiet = opts->quiet && !(opts->verbose || opts->build_verbose);
    ctx.keep_going = opts->keep_going;
    ctx.dry_run = opts->dry_run;
    ctx.jobs = opts->jobs > 1 ? opts->jobs : 1;
    ctx.defaults = opts->defaults;
    ctx.tool_allow = opts->tool_allow;
    ctx.tool_allow_count = opts->tool_allow_count;
    ctx.factory_names = factory_names;
    ctx.factory_count = factory_count;
    ctx.profile = opts->profile;
    ctx.cross_triple = opts->cross_triple;
    ctx.cross_cc = opts->cross_cc;
    ctx.cccc_self = opts->cccc_self;
    if (opts->build_cache) {
        ctx.cache_dir = *opts->build_cache
            ? xstrdup(opts->build_cache)
            : join(ctx.out_dir, ".cccc-cache");
        mkdir_p(ctx.cache_dir);
    }
    ctx.build_options = opts->build_options;
    ctx.build_options_count = opts->build_options_count;
    ctx.build_install = opts->build_install;
    ctx.user_args       = opts->user_args;
    ctx.user_args_count = opts->user_args_count;
    {
        const char *prefix_env = getenv("PREFIX");
        ctx.install_prefix = xstrdup(prefix_env ? prefix_env : "/usr/local");
    }

    s_ctx = &ctx;
    cc_run_at(vm, (Pc)fn->code_addr, 0, NULL);
    long long ret = vm->regs[REG_A0];
    s_ctx = NULL;

    int exit_code;
    if (ctx.run_invoked)
        exit_code = ctx.failed ? 1 : 0;
    else
        exit_code = (int)ret;

    if (exit_code == 0 && ctx.build_install && ctx.install_count > 0)
        if (run_install(&ctx) != 0) exit_code = 1;

    for (int i = 0; i < ctx.targets_count; i++)
        free_target(ctx.targets[i]);
    free(ctx.targets);
    free(ctx.out_dir);
    free(ctx.cache_dir);
    for (int i = 0; i < ctx.captures_count; i++) free(ctx.captures[i]);
    free(ctx.captures);
    free(ctx.install_prefix);
    free(ctx.install_targets);
    if (ctx.original_cwd) { chdir(ctx.original_cwd); free(ctx.original_cwd); }
    free(factory_names);
    return exit_code;
}
