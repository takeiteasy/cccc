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

#include "./internal.h"
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

void argv_push(ArgVec *args, const char *arg) {
    if (args->len + 1 >= args->cap) {
        int          new_cap  = args->cap ? args->cap * 2 : 16;
        const char **new_data = realloc(args->data, sizeof(char *) * new_cap);
        if (!new_data)
            error("failed to allocate argument vector");
        args->data = new_data;
        args->cap  = new_cap;
    }
    args->data[args->len++] = arg;
    args->data[args->len]   = NULL;
}

char *make_tmp_path(const char *suffix) {
#if defined(_WIN32)
    (void)suffix;
    return NULL;
#else
    char template[] = "/tmp/cccc-native-XXXXXX";
    int  fd         = mkstemp(template);
    if (fd < 0)
        return NULL;
    close(fd);

    size_t len  = strlen(template) + strlen(suffix) + 1;
    char  *path = malloc(len);
    if (!path) {
        unlink(template);
        return NULL;
    }
    snprintf(path, len, "%s%s", template, suffix);
    if (rename(template, path) != 0) {
        unlink(template);
        free(path);
        return NULL;
    }
    return path;
#endif
}

#if !defined(_WIN32)
extern char **environ;

// Merge the process environment with a NULL-terminated array of "NAME=VALUE"
// overrides (extra_env). Entries in extra_env shadow same-named entries from
// environ. Returns NULL if extra_env is NULL/empty (caller should fall back
// to plain execvp, which inherits environ unmodified). The returned array's
// pointers alias environ/extra_env strings — only the array itself is owned
// by the caller.
static char **merge_env(char *const extra_env[]) {
    if (!extra_env || !extra_env[0])
        return NULL;
    int base_count = 0;
    while (environ[base_count])
        base_count++;
    int extra_count = 0;
    while (extra_env[extra_count])
        extra_count++;
    char **merged =
        malloc(sizeof(char *) * (size_t)(base_count + extra_count + 1));
    if (!merged)
        return NULL;
    int n = 0;
    for (int i = 0; i < base_count; i++) {
        int shadowed = 0;
        for (int j = 0; j < extra_count; j++) {
            const char *eq = strchr(extra_env[j], '=');
            size_t      namelen =
                eq ? (size_t)(eq - extra_env[j]) : strlen(extra_env[j]);
            if (strncmp(environ[i], extra_env[j], namelen) == 0 &&
                environ[i][namelen] == '=') {
                shadowed = 1;
                break;
            }
        }
        if (!shadowed)
            merged[n++] = environ[i];
    }
    for (int j = 0; j < extra_count; j++)
        merged[n++] = extra_env[j];
    merged[n] = NULL;
    return merged;
}
#endif

// Like run_argv, but with an optional NULL-terminated "NAME=VALUE" array of
// environment overrides applied on top of the process environment (envp may
// be NULL for "inherit environment unmodified", identical to run_argv).
// Used by the build.c SetTargetEnv() API (#842) — e.g. AFL_USE_ASAN=1 for the
// afl-asan target — since neither RunCustom's vendored shell nor run_argv's
// plain execvp gives a target's compiler child its own environment.
int run_argv_env(char *const argv[], char *const envp[]) {
#if defined(_WIN32)
    (void)argv;
    (void)envp;
    fprintf(stderr, "error: -c=native is not supported on Windows yet\n");
    return 1;
#else
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "error: failed to fork native compiler: %s\n",
                strerror(errno));
        return 1;
    }
    if (pid == 0) {
        char **merged = merge_env(envp);
        if (merged)
            execve(argv[0], argv, merged);
        else
            execvp(argv[0], argv);
        fprintf(stderr, "error: failed to execute %s: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "error: failed to wait for %s: %s\n", argv[0],
                strerror(errno));
        return 1;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
#endif
}

int run_argv(char *const argv[]) {
    return run_argv_env(argv, NULL);
}

// #1053: used to probe a host cc for which -std= spelling it accepts,
// without spilling the (expected, often-rejected) diagnostic from a
// rejected rung onto the user's terminal -- stdout/stderr are redirected
// to /dev/null in the child before exec, unlike run_argv_env().
int run_argv_quiet(char *const argv[]) {
#if defined(_WIN32)
    (void)argv;
    return 1;
#else
    pid_t pid = fork();
    if (pid < 0)
        return 1;
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return 1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 1;
#endif
}

// ============================================================================
// Host `-std=` ladder (#1053/#1073/#1187/#1218/#1273/#1274)
// ============================================================================
//
// Shared by -c=native (run_native_backend(), src/main.c) and --build native
// targets (push_compile_flags(), src/build.c): both hand a fixed-dialect
// emitted/compiled TU to a host cc and need to know which -std=<spelling> (if
// any) that specific host compiler actually accepts.
//
// CCCC's own frontend is uniformly permissive across every C standard it
// parses, and the -c=native serializer emits a fixed GNU C11 floor no matter
// what --std= was passed (see man/NATIVE.md) -- so a strict ISO `c<NN>`
// spelling forwarded to the host compiler is a promise the rest of CCCC does
// not keep. A real host GCC's strict `-std=c89` rejects constructs (`//`
// comments, mixed declarations, VLAs, compound literals, designated
// initializers) that CCCC's own `--std=c89` only pedantic-warns on -- the
// identical guest program that compiled and ran fine under the VM would then
// fail to compile natively for a dialect reason CCCC itself never enforced.
// CCCC_STD_PROBE_PREFER_GNU (-c=native only, #1187) tries "gnu<NN>" before
// "c<NN>", falling back to "c<NN>" only if the host rejects it, restoring
// "VM passes => native passes" without weakening anything: a host that
// accepts neither spelling still gets nothing forwarded. --build targets
// compile the user's own source rather than serializer output, so that
// rationale does not transfer -- omitting the flag keeps the ladder to
// alias spellings of the user's own prefix (#1274).
//
// CCCC_STD_PROBE_EXPLICIT (#1073): when the user passes --std= explicitly,
// only the equivalent-spelling rungs of THAT standard are tried (e.g.
// "23"/"2x", both CCCC_STD_C23) -- the ladder never descends to an older
// standard. Omitting it selects the full best-effort descend-to-older ladder
// the implicit-default path uses (nothing was named, so falling back is
// fine).
//
// CCCC_STD_PROBE_C11_FLOOR (-c=native only, #1273): the serializer's fixed
// GNU C11 floor (_Atomic, _Thread_local, _Alignas/_Alignof, _Static_assert,
// _Complex) is emitted unconditionally regardless of --std=, so forwarding a
// pre-C11 spelling (e.g. -std=gnu99) mis-describes the file to the host --
// the identical invariant violation CCCC_STD_PROBE_PREFER_GNU fixed one rung
// up. With this flag, a c_std below CCCC_STD_C11 is treated as CCCC_STD_C11
// when building the candidate list; --build targets compile the user's own
// (possibly genuinely pre-C11) source, so they never pass it.
// CCCC_STD_PROBE_* flags themselves are declared in internal.h (shared with
// src/build.c).
//
// Every spelling a given (vm, flags) request would try, in probe order.
// Returns the candidate count; writes at most `cap` NUL-terminated spellings
// into `out[i]` (each sized [16]). Pure function of vm->compiler.c_std,
// vm->compiler.c_std_gnu and flags -- independent of which host cc is being
// probed, so this is shared by the prober and by the "spellings tried"
// diagnostic list.
static int build_std_candidates(VirtualMachine *vm, int flags, char out[][16],
                                int cap) {
    CStdVersion c_std = vm->compiler.c_std;
    if ((flags & CCCC_STD_PROBE_C11_FLOOR) &&
        (c_std == CCCC_STD_C89 || c_std == CCCC_STD_C99))
        c_std = CCCC_STD_C11;

    const char *prefixes[2];
    int         prefix_n = 0;
    if (flags & CCCC_STD_PROBE_PREFER_GNU) {
        prefixes[prefix_n++] = "gnu";
        if (!vm->compiler.c_std_gnu)
            prefixes[prefix_n++] = "c";
    } else {
        prefixes[prefix_n++] = vm->compiler.c_std_gnu ? "gnu" : "c";
    }

    bool        explicit_std = flags & CCCC_STD_PROBE_EXPLICIT;
    const char *suffixes[6];
    int         n = 0;
    switch (c_std) {
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

    // Prefix-major, not suffix-major (#1218): try every "gnu<suffix>" rung
    // before any "c<suffix>" one, so a host that accepts a strict ISO `c23`
    // but also `gnu2x` gets the GNU spelling. The "c" prefix is only in the
    // list at all when PREFER_GNU is set and the user explicitly typed
    // `c<NN>` (c_std_gnu == false), which forces explicit_std, which
    // restricts `suffixes` to aliases of that one standard -- so no
    // ordering here can downgrade the standard, only the GNU-ness.
    int count = 0;
    for (int p = 0; p < prefix_n && count < cap; p++)
        for (int i = 0; i < n && count < cap; i++)
            snprintf(out[count++], 16, "%s%s", prefixes[p], suffixes[i]);
    return count;
}

void cccc_host_std_spellings(VirtualMachine *vm, int flags, char *buf,
                             size_t n) {
    char cand[12][16];
    int  count = build_std_candidates(vm, flags, cand, 12);
    buf[0]     = '\0';
    size_t len = 0;
    for (int i = 0; i < count; i++) {
        if (len && len + 2 < n)
            len += (size_t)snprintf(buf + len, n - len, ", ");
        if (len < n)
            len += (size_t)snprintf(buf + len, n - len, "%s", cand[i]);
    }
}

// #1274: -c=native can call this at most a handful of times per process with
// a small, fixed set of (cc, flags) pairs; --build can call it once per
// target, each potentially resolving a different compiler
// (effective_cc_for_target()) -- unlike the single-cc, single-flags memo
// this replaces, the cache must be keyed on both, or a later target could
// silently reuse an earlier, unrelated target's probed spelling.
#define CCCC_STD_PROBE_CACHE_CAP 8
typedef struct {
    char cc[512];
    int  flags;
    bool has_result;
    bool found;
    char resolved[16];
} CcccStdProbeCacheEntry;

static CcccStdProbeCacheEntry cccc_std_probe_cache[CCCC_STD_PROBE_CACHE_CAP];
static int                    cccc_std_probe_cache_len = 0;

const char *cccc_resolve_host_std(VirtualMachine *vm, const char *cc,
                                  int flags) {
    for (int i = 0; i < cccc_std_probe_cache_len; i++) {
        CcccStdProbeCacheEntry *e = &cccc_std_probe_cache[i];
        if (e->flags == flags && strcmp(e->cc, cc) == 0)
            return e->has_result && e->found ? e->resolved : NULL;
    }

    char cand[12][16];
    int  count = build_std_candidates(vm, flags, cand, 12);
    bool found = false;
    int  hit   = -1;
    for (int i = 0; i < count && !found; i++) {
        char probe_flag[24];
        snprintf(probe_flag, sizeof(probe_flag), "-std=%s", cand[i]);
        char *probe_argv[] = {(char *)cc, "-fsyntax-only", probe_flag, "-x",
                              "c",        "/dev/null",     NULL};
        if (run_argv_quiet(probe_argv) == 0) {
            found = true;
            hit   = i;
        }
    }

    // A full cache table (CCCC_STD_PROBE_CACHE_CAP distinct (cc, flags)
    // pairs probed in one process) re-probes on every further call rather
    // than caching -- correct either way, just no longer O(1); a real run
    // never approaches 8 distinct pairs (one or two host compilers, a
    // handful of flag combinations).
    if (cccc_std_probe_cache_len < CCCC_STD_PROBE_CACHE_CAP) {
        CcccStdProbeCacheEntry *e =
            &cccc_std_probe_cache[cccc_std_probe_cache_len++];
        snprintf(e->cc, sizeof(e->cc), "%s", cc);
        e->flags      = flags;
        e->has_result = true;
        e->found      = found;
        if (found)
            snprintf(e->resolved, sizeof(e->resolved), "%s", cand[hit]);
        return found ? e->resolved : NULL;
    }

    // Cache overflow: still correct, just answers from a static scratch
    // buffer instead of a cache slot (single caller per return value, so no
    // aliasing hazard -- the caller uses/copies it before calling again).
    static char overflow_resolved[16];
    if (found)
        snprintf(overflow_resolved, sizeof(overflow_resolved), "%s", cand[hit]);
    return found ? overflow_resolved : NULL;
}

// #1273: even with no --std= forwarded at all (implicit path, ladder found
// nothing), the serializer's fixed GNU C11 floor still has to land on
// something the host's own DEFAULT dialect accepts. One -fsyntax-only probe
// against a tiny TU exercising exactly that floor (_Static_assert,
// _Thread_local, _Atomic, _Alignas/_Alignof, _Complex) -- a pass means the
// host's default dialect is safe to compile the emitted file under.
bool cccc_host_default_has_c11(const char *cc) {
    static char cached_cc[512];
    static bool has_result = false;
    static bool result     = false;
    if (has_result && strcmp(cached_cc, cc) == 0)
        return result;

    char *probe_path = make_tmp_path(".c");
    if (!probe_path)
        return true; // fail open: don't block a compile on a tmpfile error
    FILE *f = fopen(probe_path, "w");
    if (!f) {
        unlink(probe_path);
        free(probe_path);
        return true;
    }
    fputs("_Static_assert(1, \"\");\n"
          "_Thread_local int __cccc_c11_probe_tls;\n"
          "_Atomic int __cccc_c11_probe_atomic;\n"
          "_Alignas(16) int __cccc_c11_probe_align;\n"
          "int __cccc_c11_probe_alignof = _Alignof(int);\n"
          "double _Complex __cccc_c11_probe_complex;\n",
          f);
    fclose(f);

    char *probe_argv[] = {(char *)cc, "-fsyntax-only", probe_path, NULL};
    result             = run_argv_quiet(probe_argv) == 0;
    unlink(probe_path);
    free(probe_path);

    snprintf(cached_cc, sizeof(cached_cc), "%s", cc);
    has_result = true;
    return result;
}
