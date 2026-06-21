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

#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _POSIX_VERSION
#include <unistd.h>
#endif

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
} CcTargetKind;

typedef struct BuildTarget BuildTarget;
typedef struct Builder Builder;

struct BuildTarget {
    CcTargetKind kind;
    char *name;
    char *output;          // explicit output path (relative to out_dir) or NULL
    StringArray sources;
    StringArray includes;
    StringArray defines;   // "NAME=VALUE" or "NAME"
    StringArray undefs;
    StringArray cflags;
    StringArray ldflags;
    StringArray libs;      // bare -l names
    StringArray libpaths;  // -L paths
    BuildTarget **deps;
    int deps_count, deps_cap;
    int visited;           // topo-sort marker: 0 unvisited, 1 in-progress, 2 done
};

struct Builder {
    char *root;
    char *out_dir;
    const char *host;
    const char *target_filter; // --build-target=NAME, or NULL (build all)
    int verbose;
    int dry_run;
    BuildTarget **targets;
    int targets_count, targets_cap;
    const CcNativeCompileArgs *defaults; // CLI -I/-D/-U/--std/-l/-L forwarded
    int run_invoked;       // set once a cccc_build_run* is called
    int failed;            // non-zero once any build step fails
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
    }
    return xstrdup(buf);
}

// ============================================================================
// Builder API — FFI-callable.  Pointers are marshalled as int64; the cosmetic
// ctx handle is ignored in favour of s_ctx.
// ============================================================================

static BuildTarget *new_target(CcTargetKind kind, const char *name) {
    Builder *ctx = s_ctx;
    if (!ctx)
        error("build: target factory called outside a build run");
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
static long long impl_link_with(long long t, long long dep) {
    BuildTarget *tgt = (BuildTarget *)(intptr_t)t;
    BuildTarget *d = (BuildTarget *)(intptr_t)dep;
    if (tgt->deps_count >= tgt->deps_cap) {
        tgt->deps_cap = tgt->deps_cap ? tgt->deps_cap * 2 : 4;
        tgt->deps = realloc(tgt->deps, sizeof(*tgt->deps) * tgt->deps_cap);
        if (!tgt->deps)
            error("build: out of memory");
    }
    tgt->deps[tgt->deps_count++] = d;
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

void cc_load_build_runtime(VirtualMachine *vm) {
    cc_register_cfunc(vm, "__builtin_build_executable", (void *)impl_executable,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_static_lib", (void *)impl_static_lib,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_dynamic_lib",(void *)impl_dynamic_lib,        2, 0);
    cc_register_cfunc(vm, "__builtin_build_set_output", (void *)impl_set_output,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_source", (void *)impl_add_source,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_include",(void *)impl_add_include,        2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_define", (void *)impl_add_define,         3, 0);
    cc_register_cfunc(vm, "__builtin_build_add_undef",  (void *)impl_add_undef,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_cflag",  (void *)impl_add_cflag,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_ldflag", (void *)impl_add_ldflag,         2, 0);
    cc_register_cfunc(vm, "__builtin_build_link_with",  (void *)impl_link_with,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_lib",    (void *)impl_add_lib,            2, 0);
    cc_register_cfunc(vm, "__builtin_build_add_libpath",(void *)impl_add_libpath,        2, 0);
    cc_register_cfunc(vm, "__builtin_build_root",       (void *)impl_build_root,         1, 0);
    cc_register_cfunc(vm, "__builtin_build_out_dir",    (void *)impl_build_out_dir,      1, 0);
    cc_register_cfunc(vm, "__builtin_build_host",       (void *)impl_build_host,         1, 0);
    cc_register_cfunc(vm, "__builtin_build_verbose",    (void *)impl_build_verbose,      1, 0);
    cc_register_cfunc(vm, "__builtin_build_run",        (void *)impl_build_run,          2, 0);
    cc_register_cfunc(vm, "__builtin_build_run_all",    (void *)impl_build_run_all,      1, 0);
    cc_register_cfunc(vm, "__builtin_build_run_default",(void *)impl_build_run_default,  1, 0);
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

// Run (or, in dry-run, just print) one toolchain command.  Returns its exit code.
static int run_step(Builder *ctx, int n, int total, ArgVec *args) {
    print_cmd(n, total, (char *const *)args->data);
    fflush(stdout);
    if (ctx->dry_run)
        return 0;
    return run_argv((char *const *)args->data);
}

// Compile one target's sources to object files; collect the .o paths in `objs`.
// Returns 0 on success.
static int compile_sources(Builder *ctx, const char *cc,
                           BuildTarget *t, const char *objdir,
                           StringArray *objs, int *step, int total) {
    StringArray owned = {0};
    int rc = 0;
    for (int i = 0; i < t->sources.len && rc == 0; i++) {
        char *stem = stem_of(t->sources.data[i]);
        char ofile[1024];
        snprintf(ofile, sizeof(ofile), "%s/%s.o", objdir, stem);
        free(stem);
        ArgVec a = {0};
        argv_push(&a, cc);
        argv_push(&a, "-c");
        argv_push(&a, t->sources.data[i]);
        argv_push(&a, "-o");
        argv_push(&a, ofile);
        push_compile_flags(&a, ctx, t, &owned);
        rc = run_step(ctx, ++(*step), total, &a);
        free(a.data);
        strarray_push(objs, xstrdup(ofile));
    }
    free_strarray(&owned);
    return rc;
}

// Build a single target (its sources already; deps assumed built).  Returns 0 ok.
static int build_target(Builder *ctx, const char *cc,
                        BuildTarget *t, int *step, int total) {
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

    StringArray objs = {0};
    int rc = compile_sources(ctx, cc, t, tobjdir, &objs, step, total);
    if (rc != 0)
        goto done;

    if (t->kind == CCCC_TGT_STATIC) {
        char *ar = cccc_find_native_tool("ar");
        if (!ar) { rc = 1; goto done; }
        ArgVec a = {0};
        argv_push(&a, ar);
        argv_push(&a, "rcs");
        argv_push(&a, out_abs);
        for (int i = 0; i < objs.len; i++)
            argv_push(&a, objs.data[i]);
        rc = run_step(ctx, ++(*step), total, &a);
        free(a.data);
        free(ar);
    } else {
        // Executable or dynamic library: link with cc.
        ArgVec a = {0};
        argv_push(&a, cc);
        if (t->kind == CCCC_TGT_DYNAMIC)
            argv_push(&a, "-shared");
        argv_push(&a, "-o");
        argv_push(&a, out_abs);
        for (int i = 0; i < objs.len; i++)
            argv_push(&a, objs.data[i]);
        // Link against dependency libraries via -L<out>/lib -l<dep>.
        char *libdir = join(ctx->out_dir, "lib");
        int have_libdir = 0;
        for (int i = 0; i < t->deps_count; i++) {
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
        StringArray owned = {0};
        for (int i = 0; i < t->libs.len; i++) {
            char *f = malloc(strlen(t->libs.data[i]) + 3);
            snprintf(f, strlen(t->libs.data[i]) + 3, "-l%s", t->libs.data[i]);
            strarray_push(&owned, f);
            argv_push(&a, f);
        }
        for (int i = 0; i < t->ldflags.len; i++)
            argv_push(&a, t->ldflags.data[i]);
        rc = run_step(ctx, ++(*step), total, &a);
        free(a.data);
        free_strarray(&owned);
        free(libdir);
    }

done:
    free_strarray(&objs);
    free(out_rel); free(out_abs); free(out_dir); free(tobjdir);
    return rc;
}

// Count the total number of toolchain steps for the selected target set.
static int count_steps(BuildTarget **order, int n) {
    int total = 0;
    for (int i = 0; i < n; i++)
        total += order[i]->sources.len + 1; // one compile per source + 1 link/ar
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
    for (int i = 0; i < ctx->targets_count; i++)
        ctx->targets[i]->visited = 0;

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

    int total = count_steps(order, n);
    int step = 0;
    int failures = 0;
    for (int i = 0; i < n; i++) {
        if (build_target(ctx, cc, order[i], &step, total) != 0) {
            fprintf(stderr, "build: target '%s' failed\n", order[i]->name);
            failures++;
            break; // serial runner stops at first failure
        }
    }
    free(cc);
    free(order);

    if (failures == 0)
        printf("build succeeded (%d target%s, 0 errors)\n", n, n == 1 ? "" : "s");
    else {
        printf("build failed (%d error%s)\n", failures, failures == 1 ? "" : "s");
        ctx->failed = 1;
    }
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
    free_strarray(&t->sources);
    free_strarray(&t->includes);
    free_strarray(&t->defines);
    free_strarray(&t->undefs);
    free_strarray(&t->cflags);
    free_strarray(&t->ldflags);
    free_strarray(&t->libs);
    free_strarray(&t->libpaths);
    free(t->deps);
    free(t);
}

int cc_run_build(VirtualMachine *vm, Obj *prog, const CcBuildOptions *opts) {
    const char *entry = resolve_entry(vm, opts->entry_name);
    if (!entry)
        return 1;

    Obj *fn = find_fn(prog, entry);
    if (!fn || !fn->is_function) {
        fprintf(stderr, "build: entry '%s' not found\n", entry);
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
    ctx.verbose = opts->verbose;
    ctx.dry_run = opts->dry_run;
    ctx.defaults = opts->defaults;

    s_ctx = &ctx;
    cc_run_at(vm, (Pc)fn->code_addr, 0, NULL);
    long long ret = vm->regs[REG_A0];
    s_ctx = NULL;

    int exit_code;
    if (ctx.run_invoked)
        exit_code = ctx.failed ? 1 : 0;
    else
        exit_code = (int)ret;

    for (int i = 0; i < ctx.targets_count; i++)
        free_target(ctx.targets[i]);
    free(ctx.targets);
    free(ctx.out_dir);
    return exit_code;
}
