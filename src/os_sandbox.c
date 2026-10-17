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
// TODO(#1666): reads are unrestricted and exec is allowed by toolchain path,
// not by binary; a read allowlist built from the include paths and inputs
// would close the first.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "os_sandbox.h"
#include "internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ROOTS 64

typedef struct {
    char *write_roots[MAX_ROOTS];
    int   write_count;
    char *exec_roots[MAX_ROOTS];
    int   exec_count;
} Roots;

static void add_root(char **list, int *count, const char *path) {
    char resolved[PATH_MAX];
    if (!path || !*path || !realpath(path, resolved) || *count >= MAX_ROOTS)
        return;
    for (int i = 0; i < *count; i++)
        if (!strcmp(list[i], resolved))
            return;
    list[(*count)++] = strdup(resolved);
}

static void add_write(Roots *r, const char *path) {
    add_root(r->write_roots, &r->write_count, path);
}

static void add_exec(Roots *r, const char *path) {
    add_root(r->exec_roots, &r->exec_count, path);
}

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

static void add_write_dir_of(Roots *r, const char *file) {
    if (!file)
        return;
    char *copy  = strdup(file);
    char *slash = strrchr(copy, '/');
    if (!slash)
        add_write(r, ".");
    else if (slash == copy)
        add_write(r, "/");
    else {
        *slash = '\0';
        add_write(r, copy);
    }
    free(copy);
}

static void free_roots(Roots *r) {
    for (int i = 0; i < r->write_count; i++)
        free(r->write_roots[i]);
    for (int i = 0; i < r->exec_count; i++)
        free(r->exec_roots[i]);
}

// Host C compiler and the programs it runs, resolved before the sandbox closes.
static void add_toolchain(Roots *r) {
    const char *env = getenv("CCCC_NATIVE_CC");
    char       *cc  = env && *env ? cccc_path_find_executable(env) : NULL;
    const char *fallback[] = {"cc", "clang", "gcc"};
    for (size_t i = 0; !cc && i < sizeof fallback / sizeof *fallback; i++)
        cc = cccc_path_find_executable(fallback[i]);
    if (!cc)
        return;
    add_exec(r, cc);
#if defined(__APPLE__)
    add_exec(r, "/usr/bin/xcrun");
    add_exec(r, "/Library/Developer/CommandLineTools");
    FILE *p = popen("xcode-select -p 2>/dev/null", "r");
    if (p) {
        char dir[PATH_MAX];
        if (fgets(dir, sizeof dir, p)) {
            dir[strcspn(dir, "\n")] = '\0';
            add_exec(r, dir);
        }
        pclose(p);
    }
#else
    const char *progs[] = {"cc1", "as", "ld", "collect2", "lto-wrapper"};
    for (size_t i = 0; i < sizeof progs / sizeof *progs; i++) {
        char cmd[PATH_MAX + 64];
        snprintf(cmd, sizeof cmd, "'%s' -print-prog-name=%s 2>/dev/null", cc,
                 progs[i]);
        FILE *p = popen(cmd, "r");
        if (!p)
            continue;
        char path[PATH_MAX];
        if (fgets(path, sizeof path, p)) {
            path[strcspn(path, "\n")] = '\0';
            if (path[0] == '/')
                add_exec(r, path);
        }
        pclose(p);
    }
#endif
    free(cc);
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
    // The ELF interpreter and the compiler's helper programs live here.
    const char *libs[] = {"/lib", "/lib64", "/usr/lib", "/usr/lib64"};
    for (size_t i = 0; i < sizeof libs / sizeof *libs; i++)
        add_exec(r, libs[i]);
#endif
    add_exec(r, p->self);
    add_toolchain(r);
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

    size_t size    = 4096 + (size_t)(roots.write_count + roots.exec_count) *
                                (PATH_MAX * 2 + 64);
    char  *profile = malloc(size);
    size_t len     = 0;
    len += (size_t)snprintf(profile, size,
                            "(version 1)(allow default)(deny network*)"
                            "(deny file-write*)"
                            "(allow file-write* (regex #\"^/dev/\")");
    for (int i = 0; i < roots.write_count; i++)
        sbpl_subpath(profile, size, &len, roots.write_roots[i]);
    len += (size_t)snprintf(profile + len, size - len, ")(deny process-exec*)");
    if (roots.exec_count) {
        len +=
            (size_t)snprintf(profile + len, size - len, "(allow process-exec*");
        for (int i = 0; i < roots.exec_count; i++)
            sbpl_subpath(profile, size, &len, roots.exec_roots[i]);
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
    // Everything except reading: execute, write, create, remove, rename.
    uint64_t handled = LL_FS_EXECUTE | LL_FS_WRITE_FILE | 0x1ff0ULL;
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
    uint64_t file_bits = LL_FS_EXECUTE | LL_FS_WRITE_FILE | LL_FS_TRUNCATE;
    for (int pass = 0; pass < 2; pass++) {
        int    n     = pass ? r->exec_count : r->write_count;
        char **paths = pass ? (char **)r->exec_roots : (char **)r->write_roots;
        for (int i = 0; i < n; i++) {
            int pfd = open(paths[i], O_PATH | O_CLOEXEC);
            if (pfd < 0)
                continue;
            struct stat st;
            bool        is_dir = fstat(pfd, &st) == 0 && S_ISDIR(st.st_mode);
            uint64_t want = pass ? LL_FS_EXECUTE : (handled & ~LL_FS_EXECUTE);
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

// Inet sockets fail with EPERM; AF_UNIX still works.
static bool seccomp_no_network(char *err, size_t err_size) {
#ifdef SB_AUDIT_ARCH
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SB_AUDIT_ARCH, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socket, 0, 3),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_UNIX, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & 0xffff)),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog prog = {.len    = sizeof filter / sizeof *filter,
                              .filter = filter};
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
