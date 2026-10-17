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

// Config files: ~/.config/cccc/config.toml (or $XDG_CONFIG_HOME) /
// ~/.cccc.toml, and the nearest .cccc.toml above the working directory. Each
// top-level key becomes command-line options placed before the real
// arguments. See docs/CONFIG.md.

#include "config.h"
#include "sha256.h"
#include "toml.h"

#include <stdbool.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NO_PROMPT_ENV "CCCC_CONFIG_NO_PROMPT"

static const struct {
    const char *key;
    CfgClass    cls;
} classes[] = {
    // Modes and actions.
    {"debug", CFG_CLI_ONLY},
    {"g", CFG_CLI_ONLY},
    {"help", CFG_CLI_ONLY},
    {"h", CFG_CLI_ONLY},
    {"version", CFG_CLI_ONLY},
    {"repl", CFG_CLI_ONLY},
    {"r", CFG_CLI_ONLY},
    {"build", CFG_CLI_ONLY},
    {"b", CFG_CLI_ONLY},
    {"build-entry", CFG_CLI_ONLY},
    {"build-target", CFG_CLI_ONLY},
    {"build-dry-run", CFG_CLI_ONLY},
    {"build-list-targets", CFG_CLI_ONLY},
    {"build-install", CFG_CLI_ONLY},
    {"test", CFG_CLI_ONLY},
    {"test-run", CFG_CLI_ONLY},
    {"test-suite", CFG_CLI_ONLY},
    {"list-tests", CFG_CLI_ONLY},
    {"testing", CFG_CLI_ONLY},
    {"t", CFG_CLI_ONLY},
    {"watch", CFG_CLI_ONLY},
    {"url-cache-clear", CFG_CLI_ONLY},
    {"disassemble", CFG_CLI_ONLY},
    {"d", CFG_CLI_ONLY},
    {"ast", CFG_CLI_ONLY},
    {"a", CFG_CLI_ONLY},
    {"print-tokens", CFG_CLI_ONLY},
    {"p", CFG_CLI_ONLY},
    {"preprocess", CFG_CLI_ONLY},
    {"E", CFG_CLI_ONLY},
    {"dump-expanded", CFG_CLI_ONLY},
    {"m", CFG_CLI_ONLY},
    {"json", CFG_CLI_ONLY},
    {"j", CFG_CLI_ONLY},
    {"ffi-decls", CFG_CLI_ONLY},
    {"J", CFG_CLI_ONLY},
    {"emit-cccc", CFG_CLI_ONLY},
    {"emit-only", CFG_CLI_ONLY},
    {"entry", CFG_CLI_ONLY},
    {"e", CFG_CLI_ONLY},
    {"no-config", CFG_CLI_ONLY},
    {"show-config", CFG_CLI_ONLY},
    {"trust", CFG_CLI_ONLY},
    {"untrust", CFG_CLI_ONLY},

    // Policy about project configs, so a project config cannot set it.
    {"trust-hash", CFG_USER_ONLY},

    // Widen what runs or where cccc writes.
    {"comptime-allow", CFG_TRUST},
    {"comptime-include-all", CFG_TRUST},
    {"library", CFG_TRUST},
    {"l", CFG_TRUST},
    {"library-path", CFG_TRUST},
    {"L", CFG_TRUST},
    {"framework-path", CFG_TRUST},
    {"F", CFG_TRUST},
    {"iframework", CFG_TRUST},
    {"link-arg", CFG_TRUST},
    {"compile", CFG_TRUST},
    {"c", CFG_TRUST},
    {"out", CFG_TRUST},
    {"o", CFG_TRUST},
    {"deps-file", CFG_TRUST},
    {"url-cache-dir", CFG_TRUST},
    {"sysroot", CFG_TRUST},
    {"use-system-headers", CFG_TRUST},
    {"build-cc", CFG_TRUST},
    {"build-tool-allow", CFG_TRUST},
    {"build-option", CFG_TRUST},
    {"build-out-dir", CFG_TRUST},
    {"build-cache", CFG_TRUST},

    // Everything else.
    {"0", CFG_FREE},
    {"1", CFG_FREE},
    {"2", CFG_FREE},
    {"3", CFG_FREE},
    {"safety", CFG_FREE},
    {"verbose", CFG_FREE},
    {"v", CFG_FREE},
    {"no-preprocess", CFG_FREE},
    {"X", CFG_FREE},
    {"no-stdlib", CFG_FREE},
    {"S", CFG_FREE},
    {"bounds-checks", CFG_FREE},
    {"B", CFG_FREE},
    {"checked-pointers", CFG_FREE},
    {"no-layout-guards", CFG_FREE},
    {"compiler-family", CFG_FREE},
    {"uaf-detection", CFG_FREE},
    {"type-checks", CFG_FREE},
    {"uninitialized-detection", CFG_FREE},
    {"overflow-checks", CFG_FREE},
    {"stack-canaries", CFG_FREE},
    {"heap-canaries", CFG_FREE},
    {"pointer-sanitizer", CFG_FREE},
    {"P", CFG_FREE},
    {"memory-leak-detection", CFG_FREE},
    {"M", CFG_FREE},
    {"stack-instrumentation", CFG_FREE},
    {"stack-errors", CFG_FREE},
    {"dangling-pointers", CFG_FREE},
    {"alignment-checks", CFG_FREE},
    {"provenance-tracking", CFG_FREE},
    {"invalid-arithmetic", CFG_FREE},
    {"format-string-checks", CFG_FREE},
    {"random-canaries", CFG_FREE},
    {"memory-poisoning", CFG_FREE},
    {"memory-tagging", CFG_FREE},
    {"no-vm-heap", CFG_FREE},
    {"V", CFG_FREE},
    {"control-flow-integrity", CFG_FREE},
    {"no-comptime", CFG_FREE},
    {"C", CFG_FREE},
    {"thread-safety", CFG_FREE},
    {"T", CFG_FREE},
    {"include", CFG_FREE},
    {"I", CFG_FREE},
    {"isystem", CFG_FREE},
    {"i", CFG_FREE},
    {"define", CFG_FREE},
    {"D", CFG_FREE},
    {"undef", CFG_FREE},
    {"U", CFG_FREE},
    {"allow-url", CFG_TRUST},
    {"sandbox", CFG_FREE},
    {"url-timeout", CFG_FREE},
    {"url-max-size", CFG_FREE},
    {"max-errors", CFG_FREE},
    {"n", CFG_FREE},
    {"embed-limit", CFG_FREE},
    {"embed-hard-limit", CFG_FREE},
    {"optimize", CFG_FREE},
    {"O", CFG_FREE},
    {"posix-emulation", CFG_FREE},
    {"macro-recursion-limit", CFG_FREE},
    {"std", CFG_FREE},
    {"s", CFG_FREE},
    {"ffi-allow", CFG_TRUST},
    {"ffi-deny", CFG_FREE},
    {"disable-ffi", CFG_FREE},
    {"ffi-errors-fatal", CFG_FREE},
    {"ffi-type-checking", CFG_FREE},
    {"vm-profile", CFG_FREE},
    {"allow-comptime-pp-bleed", CFG_FREE},
    {"asm-passthru", CFG_TRUST},
    {"A", CFG_TRUST},
    {"fail-fast", CFG_FREE},
    {"test-timeout", CFG_FREE},
    {"test-format", CFG_FREE},
    {"no-emit-tests", CFG_FREE},
    {"kernel-max-group-size", CFG_FREE},
    {"openmp", CFG_FREE},
    {"attr-target", CFG_FREE},
    {"no-debug-on-crash", CFG_FREE},
    {"build-jobs", CFG_FREE},
    {"build-keep-going", CFG_FREE},
    {"build-quiet", CFG_FREE},
    {"build-verbose", CFG_FREE},
    {"build-profile", CFG_FREE},
    {"build-triple", CFG_FREE},
    {"watch-path", CFG_FREE},
    {"watch-ignore", CFG_FREE},
    {"watch-debounce", CFG_FREE},
    {"watch-clear", CFG_FREE},
    {"watch-restart", CFG_FREE},
    {"no-builtin-includes", CFG_FREE},
    {"trap-fp-divzero", CFG_FREE},
    {"w", CFG_FREE},
    {"Werror", CFG_FREE},
    {"W", CFG_FREE},
    {"f", CFG_FREE},
    {"x", CFG_FREE},
};

#define NCLASSES (sizeof(classes) / sizeof(classes[0]))

// Keys that may hold an array, each item becoming its own option.
static const char *const list_keys[] = {
    "define",
    "D",
    "undef",
    "U",
    "include",
    "I",
    "isystem",
    "i",
    "library",
    "l",
    "library-path",
    "L",
    "framework-path",
    "F",
    "iframework",
    "W",
    "f",
    "comptime-allow",
    "ffi-allow",
    "ffi-deny",
    "watch-path",
    "watch-ignore",
    "build-tool-allow",
    "build-option",
    "link-arg",
};

// Keys whose relative values are taken from the config file's directory.
static const char *const path_keys[] = {
    "include",
    "I",
    "isystem",
    "i",
    "library-path",
    "L",
    "framework-path",
    "F",
    "iframework",
    "out",
    "o",
    "deps-file",
    "url-cache-dir",
    "sysroot",
    "build-out-dir",
    "watch-path",
};

static bool is_path_key(const char *key) {
    for (size_t i = 0; i < sizeof(path_keys) / sizeof(path_keys[0]); i++)
        if (!strcmp(path_keys[i], key))
            return true;
    return false;
}

int cccc_config_class(const char *key) {
    for (size_t i = 0; i < NCLASSES; i++)
        if (!strcmp(classes[i].key, key))
            return classes[i].cls;
    return -1;
}

static bool is_list_key(const char *key) {
    for (size_t i = 0; i < sizeof(list_keys) / sizeof(list_keys[0]); i++)
        if (!strcmp(list_keys[i], key))
            return true;
    return false;
}

// no_argument / required_argument / optional_argument for a key, or -1.
static int key_arity(const char *key) {
    if (key[0] && !key[1]) {
        const char *p = key[0] == ':' ? NULL : strchr(cccc_optstring, key[0]);
        if (!p)
            return -1;
        return p[1] == ':'
                   ? (p[2] == ':' ? optional_argument : required_argument)
                   : no_argument;
    }
    for (int i = 0; cccc_long_options[i].name; i++)
        if (!strcmp(cccc_long_options[i].name, key))
            return cccc_long_options[i].has_arg;
    return -1;
}

// The long name of a short option, so `l` and `library` grant the same thing.
static const char *canonical_key(const char *key) {
    if (!key[0] || key[1])
        return key;
    for (int i = 0; cccc_long_options[i].name; i++)
        if (cccc_long_options[i].val == (unsigned char)key[0])
            return cccc_long_options[i].name;
    return key;
}

// --- growable string vectors -------------------------------------------------

typedef struct {
    char **arg;  // option text for argv
    char **pair; // "key=value" when the option needs trust, else NULL
    int    n, cap;
} Opts;

static void opts_push(Opts *o, char *arg, char *pair) {
    if (o->n == o->cap) {
        o->cap  = o->cap ? o->cap * 2 : 16;
        o->arg  = realloc(o->arg, sizeof(*o->arg) * o->cap);
        o->pair = realloc(o->pair, sizeof(*o->pair) * o->cap);
        if (!o->arg || !o->pair) {
            fprintf(stderr, "error: out of memory\n");
            exit(1);
        }
    }
    o->arg[o->n]  = arg;
    o->pair[o->n] = pair;
    o->n++;
}

static char *xstrdup(const char *s) {
    char *r = strdup(s);
    if (!r) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    return r;
}

static char *join(const char *a, const char *b) {
    char *r = malloc(strlen(a) + strlen(b) + 1);
    if (!r) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    strcpy(r, a);
    strcat(r, b);
    return r;
}

static char *xasprintf(const char *fmt, const char *a, const char *b) {
    int   n = snprintf(NULL, 0, fmt, a, b);
    char *r = malloc((size_t)n + 1);
    if (!r) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    snprintf(r, (size_t)n + 1, fmt, a, b);
    return r;
}

// --- file lookup -------------------------------------------------------------

// A config must be a regular file (not a link) owned by the current user, so
// a file planted in a shared directory is ignored.
static bool usable_file(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
           st.st_uid == geteuid();
}

// The user's own config may be a link (dotfile managers) or owned by someone
// else (a Nix store); only the project file needs the stricter check.
static bool usable_global(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static char *global_config_dir(void) {
    const char *xdg  = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0] == '/')
        return join(xdg, "/cccc");
    if (home && home[0])
        return join(home, "/.config/cccc");
    return NULL;
}

static char *find_global(void) {
    char *dir = global_config_dir();
    if (dir) {
        char *p = join(dir, "/config.toml");
        free(dir);
        if (usable_global(p))
            return p;
        free(p);
    }
    const char *home = getenv("HOME");
    if (home && home[0]) {
        char *p = join(home, "/.cccc.toml");
        if (usable_global(p))
            return p;
        free(p);
    }
    return NULL;
}

// Nearest .cccc.toml from the working directory upward. The search stops after
// a directory holding .git, and before $HOME (whose .cccc.toml is the global
// file).
static char *find_local(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd))
        return NULL;
    char *home = getenv("HOME");
    char  home_real[PATH_MAX];
    bool  have_home = home && home[0] && realpath(home, home_real);
    for (;;) {
        if (have_home && !strcmp(cwd, home_real))
            return NULL;
        char       *p = join(cwd, "/.cccc.toml");
        struct stat st;
        if (usable_file(p))
            return p;
        if (lstat(p, &st) == 0)
            fprintf(stderr,
                    "warning: ignoring %s: not a regular file owned by you\n",
                    p);
        free(p);
        char *git  = join(cwd, "/.git");
        bool  stop = access(git, F_OK) == 0;
        free(git);
        if (stop || !strcmp(cwd, "/"))
            return NULL;
        char *slash = strrchr(cwd, '/');
        if (slash == cwd)
            cwd[1] = '\0';
        else
            *slash = '\0';
    }
}

// --- parsing -----------------------------------------------------------------

static const char *cfg_dir;   // directory of the file being parsed
static bool        cfg_local; // the file being parsed is a project config

// Emits one option for `key`; `val` is NULL for a bare flag.
static void emit(Opts *o, const char *key, const char *val, CfgClass cls) {
    char *resolved = NULL;
    if (val && val[0] != '/' && is_path_key(key)) {
        resolved = xasprintf("%s/%s", cfg_dir, val);
        val      = resolved;
    }
    bool  is_short = key[0] && !key[1];
    char *arg      = !val       ? join(is_short ? "-" : "--", key)
                     : is_short ? xasprintf("-%s%s", key, val)
                                : xasprintf("--%s=%s", key, val);
    char *pair     = NULL;
    if (cls == CFG_TRUST)
        pair = xasprintf("%s=%s", canonical_key(key), val ? val : "true");
    opts_push(o, arg, pair);
    free(resolved);
}

static void emit_text(Opts *o, toml_datum_t at, const char *key, int arity,
                      const char *val, CfgClass cls) {
    if (arity == no_argument)
        cccc_toml_fail(at, "'%s' is a flag; use true or false", key);
    if (!val[0]) {
        if (arity == required_argument)
            cccc_toml_fail(at, "'%s' needs a value", key);
        val = NULL;
    }
    emit(o, key, val, cls);
}

// Emits the option for a string, integer or boolean value.
static void emit_scalar(Opts *o, toml_datum_t at, const char *key, int arity,
                        CfgClass cls) {
    switch (at.type) {
        case TOML_BOOLEAN:
            if (arity == required_argument)
                cccc_toml_fail(at, "'%s' needs a value", key);
            if (at.u.boolean)
                emit(o, key, NULL, cls);
            break;
        case TOML_STRING:
            emit_text(o, at, key, arity, at.u.s, cls);
            break;
        case TOML_INT64: {
            char num[24];
            snprintf(num, sizeof num, "%lld", (long long)at.u.int64);
            emit_text(o, at, key, arity, num, cls);
            break;
        }
        default:
            cccc_toml_fail(at, "'%s' cannot be of type %s", key,
                           cccc_toml_type_name(at.type));
    }
}

// Parses a config file. For a project file (`local`) a user-only key is an
// error. `hex`, when given, receives the SHA-256 of the bytes parsed.
static void parse_file(const char *path, Opts *o, bool local, char *hex) {
    char *slash = strrchr(path, '/');
    char *dir   = slash ? xstrdup(path) : xstrdup(".");
    if (slash)
        dir[slash == path ? 1 : slash - path] = '\0';
    cfg_dir   = dir;
    cfg_local = local;

    size_t len;
    char  *text = cccc_toml_read(path, &len);
    if (hex)
        cccc_sha256_hex(text, len, hex);
    toml_result_t r = cccc_toml_parse_text(path, text, len);
    free(text);
    cccc_toml_check_text(r.toptab);
    for (int i = 0; i < r.toptab.u.tab.size; i++) {
        const char  *key = r.toptab.u.tab.key[i];
        toml_datum_t v   = r.toptab.u.tab.value[i];
        if (v.type == TOML_TABLE)
            cccc_toml_fail(v, "tables are not supported in a config file");
        int arity = key_arity(key);
        if (arity < 0)
            cccc_toml_fail(v, "unknown key '%s'", key);
        int cls = cccc_config_class(key);
        if (cls < 0 || cls == CFG_CLI_ONLY)
            cccc_toml_fail(v, "'%s' cannot be set in a config file", key);
        if (cls == CFG_USER_ONLY && local)
            cccc_toml_fail(v, "'%s' can only be set in the user config", key);
        if (v.type != TOML_ARRAY) {
            emit_scalar(o, v, key, arity, (CfgClass)cls);
            continue;
        }
        if (!is_list_key(key))
            cccc_toml_fail(v, "'%s' takes one value, not a list", key);
        for (int j = 0; j < v.u.arr.size; j++) {
            toml_datum_t item = v.u.arr.elem[j];
            if (item.type != TOML_STRING && item.type != TOML_INT64)
                cccc_toml_fail(item,
                               "'%s' list items must be strings, found %s", key,
                               cccc_toml_type_name(item.type));
            emit_scalar(o, item, key, arity, (CfgClass)cls);
        }
    }
    toml_free(r);
    cfg_dir = NULL;
    free(dir);
}

// --- trust store -------------------------------------------------------------

// One line per grant, "<realpath>\t<key>=<value>". A file pinned by hash also
// has "<realpath>\t@sha256=<hex>"; no option key starts with '@'.
#define HASH_KEY "@sha256="

static char *trust_file(void) {
    char *dir = global_config_dir();
    if (!dir)
        return NULL;
    char *p = join(dir, "/trusted");
    free(dir);
    return p;
}

// The trust list as one string starting with "\n"; just "\n" when absent.
static char *slurp(const char *path) {
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f)
        return xstrdup("\n");
    char  *r   = xstrdup("\n");
    size_t len = 1;
    char   chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        r = realloc(r, len + n + 1);
        if (!r) {
            fprintf(stderr, "error: out of memory\n");
            exit(1);
        }
        memcpy(r + len, chunk, n);
        len += n;
    }
    r[len] = '\0';
    fclose(f);
    return r;
}

static bool path_storable(const char *path) {
    return !strpbrk(path, "\t\n");
}

static bool is_trusted(const char *store, const char *real, const char *pair) {
    if (!path_storable(real))
        return false;
    // `store` begins with "\n", so every line is preceded by one.
    char *line  = xasprintf("\n%s\t%s\n", real, pair);
    bool  found = strstr(store, line) != NULL;
    free(line);
    return found;
}

// The SHA-256 a file was pinned with (64 hex digits, malloc'd), or NULL.
static char *stored_hash(const char *store, const char *real) {
    if (!path_storable(real))
        return NULL;
    char *prefix = xasprintf("\n%s\t" HASH_KEY "%s", real, "");
    char *at     = strstr(store, prefix);
    char *hex    = NULL;
    if (at) {
        const char *h = at + strlen(prefix);
        size_t      n = strspn(h, "0123456789abcdef");
        if (n == 64 && (h[n] == '\n' || h[n] == '\0')) {
            hex = malloc(65);
            if (hex) {
                memcpy(hex, h, 64);
                hex[64] = '\0';
            }
        }
    }
    free(prefix);
    return hex;
}

static void mkdir_p(const char *dir) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    mkdir(tmp, 0700);
}

static bool trust_append(const char *real, char *const *pairs, int n) {
    char *file = trust_file();
    if (!file || !path_storable(real))
        return false;
    char *slash = strrchr(file, '/');
    *slash      = '\0';
    mkdir_p(file);
    *slash  = '/';
    FILE *f = fopen(file, "a");
    free(file);
    if (!f)
        return false;
    for (int i = 0; i < n; i++)
        fprintf(f, "%s\t%s\n", real, pairs[i]);
    fclose(f);
    return true;
}

// Drops every line for `real`, or only its hash line when `hash_only`.
static void trust_remove(const char *real, bool hash_only) {
    char *file = trust_file();
    if (!file)
        return;
    char *store = slurp(file);
    FILE *f     = fopen(file, "w");
    free(file);
    if (!f) {
        free(store);
        return;
    }
    size_t rl = strlen(real);
    for (char *line = strtok(store, "\n"); line; line = strtok(NULL, "\n")) {
        bool mine = strncmp(line, real, rl) == 0 && line[rl] == '\t';
        if (mine &&
            (!hash_only || !strncmp(line + rl + 1, HASH_KEY, strlen(HASH_KEY))))
            continue;
        fprintf(f, "%s\n", line);
    }
    fclose(f);
    free(store);
}

// Pins `real` to `hex`, replacing any earlier pin.
static bool trust_pin(const char *real, const char *hex) {
    trust_remove(real, true);
    char *pair = xasprintf(HASH_KEY "%s%s", hex, "");
    bool  ok   = trust_append(real, &pair, 1);
    free(pair);
    return ok;
}

// --- trust decision ----------------------------------------------------------

// TODO: parallel runs each prompt and race on the trust list; lock it (#1651).
static bool prompt_tty(const char *path, bool changed, char *const *new_pairs,
                       int n) {
    const char *skip = getenv(NO_PROMPT_ENV);
    if (skip && skip[0] && strcmp(skip, "0"))
        return false;
    int fd = open("/dev/tty", O_RDWR);
    if (fd < 0)
        return false;
    FILE *tty = fdopen(fd, "r+");
    if (!tty) {
        close(fd);
        return false;
    }
    fprintf(tty, "cccc: %s %s asks for:\n", path,
            changed ? "has changed since you trusted it; it" : "");
    for (int i = 0; i < n; i++)
        fprintf(tty, "    %s\n", new_pairs[i]);
    fprintf(tty, "Trust these settings? [y/N] ");
    fflush(tty);
    char ans[16] = "";
    bool yes = fgets(ans, sizeof ans, tty) && (ans[0] == 'y' || ans[0] == 'Y');
    fclose(tty);
    return yes;
}

// Drops every option that needs trust and has not been approved, asking on
// the terminal first when `interactive`. `show` prints each pair's state
// instead of warning. `hex` is the SHA-256 of the file as parsed: a file
// pinned to a different hash has every grant unapproved, and approving it
// again repins it (as does `pin_default` for a file not yet pinned).
static void apply_trust(Opts *o, const char *path, const char *hex,
                        bool pin_default, bool interactive, bool show) {
    char  real[PATH_MAX];
    bool  have_real = realpath(path, real) != NULL;
    char *file      = trust_file();
    char *store     = slurp(file);
    free(file);

    char *pinned  = have_real ? stored_hash(store, real) : NULL;
    bool  changed = pinned && strcmp(pinned, hex) != 0;

    char *missing[256], *fresh[256];
    int   nmissing = 0, nfresh = 0;
    for (int i = 0; i < o->n; i++) {
        if (!o->pair[i])
            continue;
        bool known = have_real && is_trusted(store, real, o->pair[i]);
        bool ok    = known && !changed;
        if (show)
            printf("  %s  %s\n", ok ? "trusted  " : "untrusted", o->pair[i]);
        if (!ok && nmissing < 256)
            missing[nmissing++] = o->pair[i];
        if (!known && nfresh < 256)
            fresh[nfresh++] = o->pair[i];
    }
    if (show && pinned)
        printf("  %s\n", changed ? "changed since trusted (sha256 pin differs)"
                                 : "pinned    (sha256 matches)");
    bool granted = false;
    if (nmissing && have_real && interactive && !show)
        granted = prompt_tty(path, changed, missing, nmissing) &&
                  trust_append(real, fresh, nfresh) &&
                  (!(pinned || pin_default) || trust_pin(real, hex));
    int kept = 0;
    for (int i = 0; i < o->n; i++) {
        bool drop = false;
        if (o->pair[i]) {
            bool ok = granted || (have_real && !changed &&
                                  is_trusted(store, real, o->pair[i]));
            if (!ok) {
                drop = true;
                if (!show)
                    fprintf(stderr,
                            "warning: %s: ignoring %s (%s); run "
                            "'cccc --trust=%s'\n",
                            path, o->pair[i],
                            changed ? "changed since trusted" : "not trusted",
                            path);
            }
        }
        if (drop)
            continue;
        o->arg[kept]  = o->arg[i];
        o->pair[kept] = o->pair[i];
        kept++;
    }
    o->n = kept;
    free(pinned);
    free(store);
}

// --- entry point -------------------------------------------------------------

static const char *flag_value(const char *arg, const char *name) {
    size_t n = strlen(name);
    if (strncmp(arg, name, n) != 0)
        return NULL;
    if (arg[n] == '\0')
        return "";
    return arg[n] == '=' ? arg + n + 1 : NULL;
}

// The value of a --trust-hash[=on|off] option: -1 when `arg` is another
// option, else 1 (on) or 0 (off).
static int trust_hash_value(const char *arg) {
    const char *v = flag_value(arg, "--trust-hash");
    if (!v)
        return -1;
    if (!v[0] || !strcmp(v, "on") || !strcmp(v, "true"))
        return 1;
    if (!strcmp(v, "off") || !strcmp(v, "false"))
        return 0;
    fprintf(stderr, "error: --trust-hash takes on or off, not '%s'\n", v);
    exit(1);
}

// The last --trust-hash among `args`, else `dflt`.
static bool last_trust_hash(char *const *args, int n, bool dflt) {
    for (int i = n - 1; i >= 0; i--) {
        int v = trust_hash_value(args[i]);
        if (v >= 0)
            return v;
    }
    return dflt;
}

// The .cccc.toml a --trust/--untrust names: a file, a directory holding one,
// or (empty) the nearest project config.
static char *resolve_target(const char *spec) {
    char *path;
    if (!spec[0]) {
        path = find_local();
    } else {
        struct stat st;
        if (stat(spec, &st) == 0 && S_ISDIR(st.st_mode))
            path = join(spec, "/.cccc.toml");
        else
            path = xstrdup(spec);
        if (!usable_file(path)) {
            free(path);
            path = NULL;
        }
    }
    if (!path) {
        fprintf(stderr, "error: no usable .cccc.toml found\n");
        exit(1);
    }
    return path;
}

const char **cccc_config_expand(int *argc, const char **argv) {
    bool        skip    = false;
    bool        show    = false;
    bool        build   = false; // a build script is its own configuration
    const char *trust   = NULL;
    const char *untrust = NULL;
    int         cli_hash = -1;    // --trust-hash on the command line, if any
    for (int i = 1; i < *argc && strcmp(argv[i], "--"); i++) {
        if (!strcmp(argv[i], "--no-config"))
            skip = true;
        else if (!strcmp(argv[i], "--build") || !strcmp(argv[i], "-b"))
            build = true;
        else if (!strcmp(argv[i], "--show-config"))
            show = true;
        else if (flag_value(argv[i], "--trust"))
            trust = flag_value(argv[i], "--trust");
        else if (flag_value(argv[i], "--untrust"))
            untrust = flag_value(argv[i], "--untrust");
        else if (trust_hash_value(argv[i]) >= 0)
            cli_hash = trust_hash_value(argv[i]);
    }
    const char *env = getenv("CCCC_NO_CONFIG");
    if (env && env[0] && strcmp(env, "0"))
        skip = true;
    if (build)
        skip = true;

    if (trust || untrust) {
        char *path = resolve_target(trust ? trust : untrust);
        char  real[PATH_MAX];
        if (!realpath(path, real) || !path_storable(real)) {
            fprintf(stderr, "error: cannot resolve %s\n", path);
            exit(1);
        }
        if (untrust) {
            trust_remove(real, false);
            printf("cccc: no longer trusting %s\n", real);
            exit(0);
        }
        bool  user_default = false;
        char *global       = skip ? NULL : find_global();
        if (global) {
            Opts g = {0};
            parse_file(global, &g, false, NULL);
            user_default = last_trust_hash(g.arg, g.n, false);
        }
        Opts o = {0};
        char hex[65];
        parse_file(path, &o, true, hex);
        char *tfile  = trust_file();
        char *store  = slurp(tfile);
        char *pinned = stored_hash(store, real);
        free(tfile);
        bool  pin = cli_hash >= 0 ? cli_hash : (pinned || user_default);
        char *fresh[256];
        int   nfresh = 0;
        printf("cccc: trusting %s\n", real);
        for (int i = 0; i < o.n; i++) {
            if (!o.pair[i])
                continue;
            printf("    %s\n", o.pair[i]);
            if (!is_trusted(store, real, o.pair[i]) && nfresh < 256)
                fresh[nfresh++] = o.pair[i];
        }
        if (nfresh && !trust_append(real, fresh, nfresh)) {
            fprintf(stderr, "error: cannot write the trust list\n");
            exit(1);
        }
        if (pin) {
            if (!trust_pin(real, hex)) {
                fprintf(stderr, "error: cannot write the trust list\n");
                exit(1);
            }
            printf("    pinned by sha256 %s\n", hex);
        } else if (pinned) {
            trust_remove(real, true);
            printf("    no longer pinned by sha256\n");
        }
        exit(0);
    }

    Opts all = {0};
    if (!skip) {
        char *global = find_global();
        char *local  = find_local();
        char  greal[PATH_MAX], lreal[PATH_MAX];
        if (global && local && realpath(global, greal) &&
            realpath(local, lreal) && !strcmp(greal, lreal)) {
            free(local);
            local = NULL;
        }
        if (show) {
            printf("global: %s\n", global ? global : "(none)");
            printf("local:  %s\n", local ? local : "(none)");
        }
        if (global)
            parse_file(global, &all, false, NULL);
        if (local) {
            Opts lo = {0};
            char hex[65];
            parse_file(local, &lo, true, hex);
            apply_trust(&lo, local, hex, last_trust_hash(all.arg, all.n, false),
                        true, show);
            for (int i = 0; i < lo.n; i++)
                opts_push(&all, lo.arg[i], NULL);
        }
        if (show) {
            printf("options:");
            for (int i = 0; i < all.n; i++)
                printf(" %s", all.arg[i]);
            printf("\n");
            exit(0);
        }
    } else if (show) {
        printf("config disabled\n");
        exit(0);
    }

    if (!all.n)
        return argv;
    const char **out = malloc(sizeof(*out) * (size_t)(*argc + all.n + 1));
    if (!out) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    int n    = 0;
    out[n++] = argv[0];
    for (int i = 0; i < all.n; i++)
        out[n++] = all.arg[i];
    for (int i = 1; i < *argc; i++)
        out[n++] = argv[i];
    out[n] = NULL;
    *argc  = n;
    return out;
}
