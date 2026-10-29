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

// --sandbox: OS-level confinement of the whole process (macOS sandbox_init,
// Linux landlock + seccomp).

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "os_sandbox.h"
#include "config.h"
#include "internal.h"
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ROOTS      64
#define MAX_READ_ROOTS 512

typedef struct {
    char *write_roots[MAX_ROOTS];
    int   write_count;
    char *exec_roots[MAX_ROOTS];
    int   exec_count;
    char *interp_roots[MAX_ROOTS]; // may only start as a script's interpreter
    int   interp_count;
    char *read_roots[MAX_READ_ROOTS];
    int   read_count;
} Roots;

static void add_root(char **list, int *count, int max, const char *path) {
    char resolved[PATH_MAX];
    if (!path || !*path || !realpath(path, resolved) || *count >= max)
        return;
    for (int i = 0; i < *count; i++)
        if (!strcmp(list[i], resolved))
            return;
    list[(*count)++] = strdup(resolved);
}

static void add_write(Roots *r, const char *path) {
    add_root(r->write_roots, &r->write_count, MAX_ROOTS, path);
}

static void add_exec(Roots *r, const char *path) {
    add_root(r->exec_roots, &r->exec_count, MAX_ROOTS, path);
}

static void add_read(Roots *r, const char *path) {
    add_root(r->read_roots, &r->read_count, MAX_READ_ROOTS, path);
}

#if defined(__APPLE__)
static void add_interp(Roots *r, const char *path) {
    add_root(r->interp_roots, &r->interp_count, MAX_ROOTS, path);
}
#endif

// Directories the run will create (build output, caches) must exist before
// the sandbox closes, or there is nothing to grant.
static void add_write_created(Roots *r, const char *dir) {
    if (!dir)
        return;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s", dir);
    for (char *c = path + 1; *c; c++) {
        if (*c != '/')
            continue;
        *c = '\0';
        mkdir(path, 0777);
        *c = '/';
    }
    mkdir(path, 0777);
    add_write(r, path);
}

// The directory holding `file` ("." for a bare name).
static char *dir_of(const char *file) {
    char *copy  = strdup(file);
    char *slash = strrchr(copy, '/');
    if (!slash)
        strcpy(copy, ".");
    else if (slash == copy)
        strcpy(copy, "/");
    else
        *slash = '\0';
    return copy;
}

static void add_write_dir_of(Roots *r, const char *file) {
    if (!file)
        return;
    char *dir = dir_of(file);
    add_write(r, dir);
    free(dir);
}

static void free_roots(Roots *r) {
    for (int i = 0; i < r->write_count; i++)
        free(r->write_roots[i]);
    for (int i = 0; i < r->exec_count; i++)
        free(r->exec_roots[i]);
    for (int i = 0; i < r->interp_count; i++)
        free(r->interp_roots[i]);
    for (int i = 0; i < r->read_count; i++)
        free(r->read_roots[i]);
}

// All of `cmd`'s output (stdout and stderr as the command redirects them), or
// NULL when it cannot run.
static char *capture(const char *cmd) {
    FILE *p = popen(cmd, "r");
    if (!p)
        return NULL;
    size_t cap = 4096, len = 0;
    char  *buf = malloc(cap);
    for (;;) {
        if (cap - len < 2)
            buf = realloc(buf, cap *= 2);
        size_t n = fread(buf + len, 1, cap - len - 1, p);
        if (n == 0)
            break;
        len += n;
    }
    buf[len] = '\0';
    pclose(p);
    return buf;
}

// Appends ` '<arg>'` to cmd, shell-quoted; drops the argument when it does not
// fit.
static void append_quoted(char *cmd, size_t size, const char *arg) {
    size_t len  = strlen(cmd);
    size_t need = len + 4;
    for (const char *c = arg; *c; c++)
        need += *c == '\'' ? 4 : 1;
    if (need >= size)
        return;
    cmd[len++] = ' ';
    cmd[len++] = '\'';
    for (const char *c = arg; *c; c++) {
        if (*c == '\'') {
            memcpy(cmd + len, "'\\''", 4);
            len += 4;
        } else {
            cmd[len++] = *c;
        }
    }
    cmd[len++] = '\'';
    cmd[len]   = '\0';
}

// Adds each ':'-separated directory of `list` as readable.
static void add_read_list(Roots *r, const char *list) {
    char *copy = strdup(list), *save = NULL;
    for (char *dir = strtok_r(copy, ":\n", &save); dir;
         dir       = strtok_r(NULL, ":\n", &save))
        add_read(r, dir);
    free(copy);
}

// `cc -print-search-dirs`: the install, program and library directories.
static void add_read_search_dirs(Roots *r, const char *cc) {
    char cmd[PATH_MAX + 64];
    snprintf(cmd, sizeof cmd, "'%s' -print-search-dirs 2>/dev/null", cc);
    char *out = capture(cmd);
    if (!out)
        return;
    for (char *line = strtok(out, "\n"); line; line = strtok(NULL, "\n")) {
        const char *labels[] = {"install:", "programs:", "libraries:"};
        for (size_t i = 0; i < sizeof labels / sizeof *labels; i++) {
            size_t n = strlen(labels[i]);
            if (strncmp(line, labels[i], n))
                continue;
            const char *v = line + n;
            while (*v == ' ' || *v == '=')
                v++;
            add_read_list(r, v);
        }
    }
    free(out);
}

// The `#include <...>` search list `cc -E -v` prints.
static void add_read_include_dirs(Roots *r, const char *cc) {
    char cmd[PATH_MAX + 64];
    snprintf(cmd, sizeof cmd, "'%s' -E -v -x c /dev/null -o /dev/null 2>&1",
             cc);
    char *out = capture(cmd);
    if (!out)
        return;
    bool in_list = false;
    for (char *line = strtok(out, "\n"); line; line = strtok(NULL, "\n")) {
        if (strstr(line, "search starts here:")) {
            in_list = true;
        } else if (strstr(line, "End of search list.")) {
            in_list = false;
        } else if (in_list && line[0] == ' ') {
            while (*line == ' ')
                line++;
            char *note = strstr(line, " (");
            if (note)
                *note = '\0';
            add_read(r, line);
        }
    }
    free(out);
}

// Adds a program by path, or by name looked up on PATH as the compiler does.
static void add_exec_program(Roots *r, const char *prog) {
    if (prog[0] == '/') {
        add_exec(r, prog);
        return;
    }
    char *found = cccc_path_find_executable(prog);
    add_exec(r, found);
    free(found);
}

// Programs `cc -###` says a compile and link run. Returns how many.
static int add_exec_from_dryrun(Roots *r, const char *cc,
                                const CcccSandboxPaths *p) {
    char cmd[8192];
    snprintf(cmd, sizeof cmd, "'%s' -###", cc);
    for (int i = 0; i < p->link_args_count; i++)
        append_quoted(cmd, sizeof cmd, p->link_args[i]);
    strncat(cmd, " -x c /dev/null -o /dev/null 2>&1",
            sizeof cmd - strlen(cmd) - 1);
    char *out = capture(cmd);
    if (!out)
        return 0;
    int found = 0;
    // A command line starts with a space: clang quotes its program, gcc does
    // not and names `as` and `ld` without a directory.
    for (char *line = strtok(out, "\n"); line; line = strtok(NULL, "\n")) {
        if (line[0] != ' ')
            continue;
        char *prog = line + strspn(line, " ");
        char *end  = NULL;
        if (*prog == '"') {
            prog++;
            end = strchr(prog, '"');
        } else {
            end = prog + strcspn(prog, " ");
        }
        if (!end || end == prog)
            continue;
        *end       = '\0';
        int before = r->exec_count;
        add_exec_program(r, prog);
        found += r->exec_count > before;
    }
    free(out);
    return found;
}

// A program the compiler names by `-print-prog-name`, as an absolute path.
static void add_exec_prog_name(Roots *r, const char *cc, const char *prog) {
    char cmd[PATH_MAX + 96];
    snprintf(cmd, sizeof cmd, "'%s' -print-prog-name=%s 2>/dev/null", cc, prog);
    char *out = capture(cmd);
    if (!out)
        return;
    out[strcspn(out, "\n")] = '\0';
    if (out[0])
        add_exec_program(r, out);
    free(out);
}

// Grants the interpreter named by the `#!` line of each program already
// allowed, e.g. Xcode's `as`, which is a zsh script. macOS lets it start only
// as a script's interpreter, so the shell cannot be run directly; landlock has
// no such distinction, so on Linux it is an ordinary exec grant. A script also
// gets the path utilities toolchain wrappers use to find their own directory.
static void add_script_interpreters(Roots *r) {
    int  count = r->exec_count; // grants made below are not scanned again
    bool found_script = false;
    for (int i = 0; i < count; i++) {
        FILE *f = fopen(r->exec_roots[i], "r");
        if (!f)
            continue;
        char line[PATH_MAX + 16];
        bool script = fgets(line, sizeof line, f) && !strncmp(line, "#!", 2);
        fclose(f);
        if (!script)
            continue;
        found_script = true;
        char *save   = NULL;
        char *tok    = strtok_r(line + 2, " \t\r\n", &save);
        if (!tok)
            continue;
        char *interps[2] = {tok, NULL};
        // `#!/usr/bin/env name`: env, then the program it finds on PATH.
        const char *base = strrchr(tok, '/');
        if (base && !strcmp(base + 1, "env")) {
            char *arg = strtok_r(NULL, " \t\r\n", &save);
            while (arg && arg[0] == '-')
                arg = strtok_r(NULL, " \t\r\n", &save);
            interps[1] = arg;
        }
        for (int j = 0; j < 2; j++) {
            if (!interps[j])
                continue;
            char *found = interps[j][0] == '/'
                              ? strdup(interps[j])
                              : cccc_path_find_executable(interps[j]);
            if (found) {
#if defined(__APPLE__)
                add_interp(r, found);
#else
                add_exec(r, found);
#endif
            }
            free(found);
        }
    }
    if (found_script) {
        const char *helpers[] = {"/bin/realpath", "/usr/bin/realpath",
                                 "/usr/bin/dirname", "/usr/bin/basename"};
        for (size_t i = 0; i < sizeof helpers / sizeof *helpers; i++)
            add_exec(r, helpers[i]);
    }
}

// Host C compiler, the programs it runs and the files it reads, resolved
// before the sandbox closes. Exec is limited to the exact programs; when the
// dry run names none, whole toolchain directories are allowed instead.
static void add_toolchain(Roots *r, const CcccSandboxPaths *p) {
    char *cc = p->cc ? cccc_path_find_executable(p->cc) : NULL;
    if (!cc)
        return;
    add_exec(r, cc);
    int exact = add_exec_from_dryrun(r, cc, p);
    // gcc's collect2 runs ld and lto-wrapper itself, which -### never shows.
    const char *progs[] = {"cc1", "as", "ld", "collect2", "lto-wrapper"};
    for (size_t i = 0; i < sizeof progs / sizeof *progs; i++)
        add_exec_prog_name(r, cc, progs[i]);
    add_read_search_dirs(r, cc);
    add_read_include_dirs(r, cc);
    // The install prefix, as invoked and as resolved.
    char        resolved[PATH_MAX] = "";
    const char *spellings[] = {cc, realpath(cc, resolved) ? resolved : NULL};
    for (size_t i = 0; i < sizeof spellings / sizeof *spellings; i++) {
        if (!spellings[i])
            continue;
        char *bin    = dir_of(spellings[i]);
        char *prefix = dir_of(bin);
        if (strcmp(prefix, "/") && strcmp(prefix, "/usr"))
            add_read(r, prefix);
        free(prefix);
        free(bin);
    }
    // A package store's programs load libraries from sibling packages.
    const char *cellar = strstr(resolved, "/Cellar/");
    if (cellar) {
        char store[PATH_MAX];
        snprintf(store, sizeof store, "%.*s", (int)(cellar - resolved),
                 resolved);
        add_read(r, store);
    }
    if (!strncmp(resolved, "/nix/store/", 11))
        add_read(r, "/nix/store");
#if defined(__APPLE__)
    add_exec(r, "/usr/bin/xcrun");
    // /usr/bin/{as,ld} are shims that run the developer directory's own.
    const char *queries[] = {"xcode-select -p 2>/dev/null",
                             "xcrun --show-sdk-path 2>/dev/null"};
    for (size_t i = 0; i < sizeof queries / sizeof *queries; i++) {
        char *out = capture(queries[i]);
        if (!out)
            continue;
        out[strcspn(out, "\n")] = '\0';
        add_read(r, out);
        free(out);
    }
    const char *tools[] = {"as", "ld", "clang"};
    for (size_t i = 0; i < sizeof tools / sizeof *tools; i++) {
        char cmd[64];
        snprintf(cmd, sizeof cmd, "xcrun -f %s 2>/dev/null", tools[i]);
        char *out = capture(cmd);
        if (!out)
            continue;
        out[strcspn(out, "\n")] = '\0';
        add_exec(r, out);
        free(out);
    }
    if (!exact) {
        fprintf(stderr, "warning: --sandbox: toolchain probe failed; exec "
                        "allowed by directory\n");
        add_exec(r, "/Library/Developer/CommandLineTools");
        add_exec(r, "/usr/bin");
    }
#else
    if (!exact) {
        fprintf(stderr, "warning: --sandbox: toolchain probe failed; exec "
                        "allowed by directory\n");
        const char *dirs[] = {"/lib", "/lib64", "/usr/lib", "/usr/lib64",
                              "/usr/libexec"};
        for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++)
            add_exec(r, dirs[i]);
    }
#endif
    free(cc);
}

static void add_read_dir_of_file(Roots *r, const char *file) {
    if (!file || !strcmp(file, "-"))
        return;
    char *dir = dir_of(file);
    add_read(r, dir);
    free(dir);
}

#if !defined(__APPLE__)
// Grants an ld.so.conf file and, recursively, the files its `include` lines
// match. The depth cap stops an include cycle.
static void add_ld_conf_reads(Roots *r, const char *conf, int depth) {
    add_read(r, conf);
    FILE *f = fopen(conf, "r");
    if (!f || depth > 4) {
        if (f)
            fclose(f);
        return;
    }
    char line[PATH_MAX + 16];
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "include", 7) || (line[7] != ' ' && line[7] != '\t'))
            continue;
        char *pattern = line + 7 + strspn(line + 7, " \t");
        pattern[strcspn(pattern, " \t\r\n")] = '\0';
        char full[PATH_MAX];
        if (pattern[0] == '/')
            snprintf(full, sizeof full, "%s", pattern);
        else
            snprintf(full, sizeof full, "/etc/%s", pattern);
        glob_t g;
        if (glob(full, 0, NULL, &g) == 0)
            for (size_t i = 0; i < g.gl_pathc; i++)
                add_ld_conf_reads(r, g.gl_pathv[i], depth + 1);
        globfree(&g);
    }
    fclose(f);
}

// The /etc files the dynamic loader, name lookups and time zone code open,
// without the rest of /etc.
static void add_etc_reads(Roots *r) {
    const char *files[] = {"/etc/ld.so.cache", "/etc/nsswitch.conf",
                           "/etc/localtime", "/etc/passwd", "/etc/group"};
    for (size_t i = 0; i < sizeof files / sizeof *files; i++)
        add_read(r, files[i]);
    add_ld_conf_reads(r, "/etc/ld.so.conf", 0);
}
#endif

// Fixed system locations every run needs to read.
static void add_system_reads(Roots *r) {
#if defined(__APPLE__)
    const char *dirs[] = {"/usr/lib",
                          "/usr/share",
                          "/usr/include",
                          "/usr/libexec",
                          "/usr/bin",
                          "/System",
                          "/Library/Developer",
                          "/private/etc",
                          "/private/var/db",
                          "/private/var/select",
                          "/dev"};
#else
    add_etc_reads(r);
    const char *dirs[] = {"/lib",
                          "/lib64",
                          "/usr/lib",
                          "/usr/lib64",
                          "/usr/include",
                          "/usr/local/include",
                          "/usr/share",
                          "/dev",
                          "/proc/self",
                          "/proc/cpuinfo",
                          "/proc/meminfo",
                          "/sys/devices/system/cpu"};
#endif
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++)
        add_read(r, dirs[i]);
}

// Config files a cccc started inside the sandbox reads.
static void add_config_reads(Roots *r) {
    char *dir = cccc_config_dir();
    add_read(r, dir);
    free(dir);
    const char *home = getenv("HOME");
    if (home && *home) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/.cccc.toml", home);
        add_read(r, path);
    }
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd))
        return;
    for (;;) {
        char path[PATH_MAX + 16];
        snprintf(path, sizeof path, "%s/.cccc.toml", cwd);
        add_read(r, path);
        char *slash = strrchr(cwd, '/');
        if (!slash || slash == cwd)
            break;
        *slash = '\0';
    }
}

static void collect_roots(Roots *r, const CcccSandboxPaths *p) {
    if (p->out_file)
        add_write_dir_of(r, p->out_file);
    if (p->default_out)
        add_write(r, ".");
    add_write_dir_of(r, p->deps_file);
    add_write_created(r, p->url_cache_dir);
    add_write_created(r, p->build_out_dir);
    add_write_created(r, p->build_cache);
    const char *tmp = getenv("TMPDIR");
    add_write(r, tmp);
    add_write(r, "/tmp");
    if (!p->url_cache_dir && tmp) {
        char dir[PATH_MAX];
        snprintf(dir, sizeof dir, "%s/.cccc", tmp);
        add_write_created(r, dir);
    }
#if defined(__APPLE__)
    char cache[PATH_MAX];
    if (confstr(_CS_DARWIN_USER_CACHE_DIR, cache, sizeof cache))
        add_write(r, cache);
    if (confstr(_CS_DARWIN_USER_TEMP_DIR, cache, sizeof cache))
        add_write(r, cache);
#endif
#if defined(__linux__)
    add_write(r, "/dev/null");
    add_write(r, "/dev/tty");
    // The ELF interpreter, which the kernel executes for every program.
    const char *interps[] = {"/lib*/ld-linux*", "/lib*/ld-musl*",
                             "/lib/*/ld-linux*", "/usr/lib*/ld-linux*"};
    for (size_t i = 0; i < sizeof interps / sizeof *interps; i++) {
        glob_t g;
        if (glob(interps[i], 0, NULL, &g) == 0)
            for (size_t j = 0; j < g.gl_pathc; j++)
                add_exec(r, g.gl_pathv[j]);
        globfree(&g);
    }
#endif
    add_exec(r, p->self);
    for (int i = 0; i < p->exec_count; i++)
        add_exec(r, p->exec_paths[i]);
    if (p->exec_output && p->out_file) {
        char *dir = dir_of(p->out_file);
        add_exec(r, dir);
        free(dir);
    }
    if (p->toolchain)
        add_toolchain(r, p);
    add_script_interpreters(r);

    // Readable: what the run was told about, the working directory and the
    // system; every write and exec root can be read too.
    add_read(r, ".");
    for (int i = 0; i < p->inputs_count; i++)
        add_read_dir_of_file(r, p->inputs[i]);
    for (int i = 0; i < p->read_count; i++)
        add_read(r, p->read_paths[i]);
    add_read(r, p->self);
    add_config_reads(r);
    add_system_reads(r);
    for (int i = 0; i < r->write_count; i++)
        add_read(r, r->write_roots[i]);
    for (int i = 0; i < r->exec_count; i++)
        add_read(r, r->exec_roots[i]);
    for (int i = 0; i < r->interp_count; i++)
        add_read(r, r->interp_roots[i]);
}

#if defined(__APPLE__)

#include <sandbox.h>

// In libsystem_sandbox but missing from the public header.
extern int sandbox_check(pid_t pid, const char *operation, int type, ...);

// Appends ` (subpath "<path>")` with the path escaped for SBPL.
static void sbpl_subpath(char *out, size_t size, size_t *len,
                         const char *path) {
    *len += (size_t)snprintf(out + *len, size - *len, " (subpath \"");
    for (const char *c = path; *c && *len + 4 < size; c++) {
        if (*c == '"' || *c == '\\')
            out[(*len)++] = '\\';
        out[(*len)++] = *c;
    }
    *len += (size_t)snprintf(out + *len, size - *len, "\")");
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
bool cccc_sandbox_enter(const CcccSandboxPaths *p, char *err, size_t err_size) {
    // A cccc this sandboxed process started (--watch restart, --build child)
    // is already confined, and macOS refuses to nest.
    if (getenv("CCCC_SANDBOXED") && sandbox_check(getpid(), NULL, 0) != 0)
        return true;

    Roots roots = {0};
    collect_roots(&roots, p);

    size_t size     = 4096 + (size_t)(roots.write_count + roots.exec_count +
                                      roots.interp_count + roots.read_count) *
                                 (PATH_MAX * 2 + 64);
    char  *profile = malloc(size);
    size_t len     = 0;
    len += (size_t)snprintf(profile, size,
                            "(version 1)(allow default)(deny network*)"
                            "(deny file-write*)"
                            "(allow file-write* (regex #\"^/dev/\")");
    for (int i = 0; i < roots.write_count; i++)
        sbpl_subpath(profile, size, &len, roots.write_roots[i]);
    len += (size_t)snprintf(profile + len, size - len,
                            ")(deny file-read-data)"
                            "(allow file-read-data (literal \"/\")"
                            "(regex #\"^/dev/\")");
    for (int i = 0; i < roots.read_count; i++)
        sbpl_subpath(profile, size, &len, roots.read_roots[i]);
    len += (size_t)snprintf(profile + len, size - len, ")(deny process-exec*)");
    if (roots.exec_count) {
        len +=
            (size_t)snprintf(profile + len, size - len, "(allow process-exec*");
        for (int i = 0; i < roots.exec_count; i++)
            sbpl_subpath(profile, size, &len, roots.exec_roots[i]);
        len += (size_t)snprintf(profile + len, size - len, ")");
    }
    if (roots.interp_count) {
        len += (size_t)snprintf(profile + len, size - len,
                                "(allow process-exec-interpreter");
        for (int i = 0; i < roots.interp_count; i++)
            sbpl_subpath(profile, size, &len, roots.interp_roots[i]);
        len += (size_t)snprintf(profile + len, size - len, ")");
    }

    char *sb_err = NULL;
    bool  ok     = sandbox_init(profile, 0, &sb_err) == 0;
    if (!ok)
        snprintf(err, err_size, "sandbox_init failed: %s",
                 sb_err ? sb_err : "unknown error");
    if (sb_err)
        sandbox_free_error(sb_err);
    free(profile);
    free_roots(&roots);
    if (ok)
        setenv("CCCC_SANDBOXED", "1", 1);
    return ok;
}
#pragma clang diagnostic pop

#elif defined(__linux__)

#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>

// landlock ABI, declared here so older kernel headers still build.
struct ll_ruleset_attr {
    uint64_t handled_access_fs;
};
struct ll_path_beneath_attr {
    uint64_t allowed_access;
    int32_t  parent_fd;
} __attribute__((packed));

#define LL_FS_EXECUTE             (1ULL << 0)
#define LL_FS_WRITE_FILE          (1ULL << 1)
#define LL_FS_READ_FILE           (1ULL << 2)
#define LL_FS_READ_DIR            (1ULL << 3)
#define LL_FS_TRUNCATE            (1ULL << 14)
#define LL_CREATE_RULESET_VERSION (1U << 0)
#define LL_RULE_PATH_BENEATH      1

#ifndef __NR_landlock_create_ruleset
#define __NR_landlock_create_ruleset 444
#define __NR_landlock_add_rule       445
#define __NR_landlock_restrict_self  446
#endif

static bool landlock_apply(const Roots *r, char *err, size_t err_size) {
    long abi = syscall(__NR_landlock_create_ruleset, NULL, 0,
                       LL_CREATE_RULESET_VERSION);
    if (abi < 1) {
        snprintf(err, err_size, "landlock is not available on this kernel");
        return false;
    }
    // Execute, read, write, create, remove, rename.
    uint64_t handled = LL_FS_EXECUTE | LL_FS_WRITE_FILE | LL_FS_READ_FILE |
                       LL_FS_READ_DIR | 0x1ff0ULL;
    if (abi >= 2)
        handled |= 1ULL << 13; // REFER
    if (abi >= 3)
        handled |= LL_FS_TRUNCATE;
    struct ll_ruleset_attr attr = {.handled_access_fs = handled};
    int fd = (int)syscall(__NR_landlock_create_ruleset, &attr, sizeof attr, 0);
    if (fd < 0) {
        snprintf(err, err_size, "landlock_create_ruleset: %s", strerror(errno));
        return false;
    }
    uint64_t file_bits =
        LL_FS_EXECUTE | LL_FS_WRITE_FILE | LL_FS_READ_FILE | LL_FS_TRUNCATE;
    const uint64_t read_bits = LL_FS_READ_FILE | LL_FS_READ_DIR;
    for (int pass = 0; pass < 3; pass++) {
        int    n     = pass == 0   ? r->write_count
                       : pass == 1 ? r->exec_count
                                   : r->read_count;
        char **paths = pass == 0   ? r->write_roots
                       : pass == 1 ? r->exec_roots
                                   : r->read_roots;
        for (int i = 0; i < n; i++) {
            int pfd = open(paths[i], O_PATH | O_CLOEXEC);
            if (pfd < 0)
                continue;
            struct stat st;
            bool        is_dir = fstat(pfd, &st) == 0 && S_ISDIR(st.st_mode);
            uint64_t    want   = pass == 0   ? (handled & ~LL_FS_EXECUTE)
                                 : pass == 1 ? LL_FS_EXECUTE
                                             : read_bits;
            struct ll_path_beneath_attr rule = {
                .allowed_access = (is_dir ? want : want & file_bits) & handled,
                .parent_fd      = pfd};
            if (rule.allowed_access)
                syscall(__NR_landlock_add_rule, fd, LL_RULE_PATH_BENEATH, &rule,
                        0);
            close(pfd);
        }
    }
    bool ok = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
              syscall(__NR_landlock_restrict_self, fd, 0) == 0;
    if (!ok)
        snprintf(err, err_size, "landlock_restrict_self: %s", strerror(errno));
    close(fd);
    return ok;
}

#if defined(__x86_64__)
#define SB_AUDIT_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define SB_AUDIT_ARCH AUDIT_ARCH_AARCH64
#endif

#ifndef AUDIT_ARCH_X86_64
#define AUDIT_ARCH_X86_64 0xc000003eU
#endif
#ifndef AUDIT_ARCH_AARCH64
#define AUDIT_ARCH_AARCH64 0xc00000b7U
#endif

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup    425
#define __NR_io_uring_enter    426
#define __NR_io_uring_register 427
#endif

#define SB_FILTER_MAX 24

// Inet sockets fail with EPERM; AF_UNIX still works. Other ways to open one
// are denied too: io_uring's socket op, x32-ABI numbers and foreign-arch
// syscalls, which a raw syscall() could otherwise use to skip the check.
static bool seccomp_no_network(char *err, size_t err_size) {
#ifdef SB_AUDIT_ARCH
    struct sock_filter f[SB_FILTER_MAX];
    int                deny_jumps[8], deny_count = 0, n = 0;
#define SB_STMT(code, k) f[n++] = (struct sock_filter)BPF_STMT(code, k)
#define SB_DENY_IF(code, k)                                                     \
    do {                                                                        \
        deny_jumps[deny_count++] = n;                                           \
        f[n++]                   = (struct sock_filter)BPF_JUMP(code, k, 0, 0); \
    } while (0)
    SB_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
    f[n++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                          SB_AUDIT_ARCH, 1, 0);
    SB_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & 0xffff));
    SB_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
#ifdef __x86_64__
    SB_DENY_IF(BPF_JMP | BPF_JGE | BPF_K, 0x40000000U);
#endif
    SB_DENY_IF(BPF_JMP | BPF_JEQ | BPF_K, __NR_io_uring_setup);
    SB_DENY_IF(BPF_JMP | BPF_JEQ | BPF_K, __NR_io_uring_enter);
    SB_DENY_IF(BPF_JMP | BPF_JEQ | BPF_K, __NR_io_uring_register);
    int socket_jump = n;
    f[n++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socket,
                                          0, 0);
    SB_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0]));
    int af_jump = n;
    f[n++] =
        (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_UNIX, 0, 0);
    int deny = n;
    SB_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & 0xffff));
    int allow = n;
    SB_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    for (int i = 0; i < deny_count; i++)
        f[deny_jumps[i]].jt = (uint8_t)(deny - deny_jumps[i] - 1);
    f[socket_jump].jf      = (uint8_t)(allow - socket_jump - 1);
    f[af_jump].jt          = (uint8_t)(allow - af_jump - 1);
#undef SB_STMT
#undef SB_DENY_IF
    struct sock_fprog prog = {.len = (unsigned short)n, .filter = f};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
        prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) == 0)
        return true;
    snprintf(err, err_size, "seccomp: %s", strerror(errno));
    return false;
#else
    snprintf(err, err_size, "no seccomp filter for this architecture");
    return false;
#endif
}

bool cccc_sandbox_enter(const CcccSandboxPaths *p, char *err, size_t err_size) {
    Roots roots = {0};
    collect_roots(&roots, p);
    bool ok = seccomp_no_network(err, err_size) &&
              landlock_apply(&roots, err, err_size);
    free_roots(&roots);
    return ok;
}

#else

bool cccc_sandbox_enter(const CcccSandboxPaths *p, char *err, size_t err_size) {
    (void)p;
    snprintf(err, err_size, "not supported on this platform");
    return false;
}

#endif
