/* unistd.h - core POSIX declarations for CCCC */

#ifndef __UNISTD_H
#define __UNISTD_H

#ifdef _WIN32
#error "<unistd.h> is only available on POSIX targets in CCCC"
#endif

#include "stddef.h"
#include "sys/types.h"

#ifndef _SSIZE_T
typedef long ssize_t;
#define _SSIZE_T
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int close(int fd);
extern off_t lseek(int fd, off_t offset, int whence);
extern int access(const char *path, int amode);
extern int unlink(const char *path);
extern int rmdir(const char *path);
extern int chdir(const char *path);
extern char *getcwd(char *buf, size_t size);
extern pid_t getpid(void);
extern pid_t getppid(void);
extern unsigned int sleep(unsigned int seconds);
extern int usleep(unsigned int useconds);
extern int pipe(int fd[2]);
extern void _exit(int status);
extern pid_t fork(void);
extern int execv(const char *path, char *const argv[]);
extern int execve(const char *path, char *const argv[], char *const envp[]);
extern int execl(const char *path, const char *arg, ...);
extern int execlp(const char *file, const char *arg, ...);
extern int execle(const char *path, const char *arg, ...);
extern int execvp(const char *file, char *const argv[]);
extern int isatty(int fd);
extern char *ttyname(int fd);
extern int dup(int fd);
extern int dup2(int oldfd, int newfd);
extern int fsync(int fd);
extern int fdatasync(int fd);
extern int ftruncate(int fd, off_t length);
extern int truncate(const char *path, off_t length);
extern long sysconf(int name);
extern long pathconf(const char *path, int name);
extern long fpathconf(int fd, int name);
extern size_t confstr(int name, char *buf, size_t len);
extern int mkstemp(char *tmpl);
extern char *mkdtemp(char *tmpl);

/* Positioned I/O */
extern ssize_t pread(int fd, void *buf, size_t count, off_t offset);
extern ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);

/* Ownership and identity */
extern int fchown(int fd, uid_t owner, gid_t group);
extern int chown(const char *path, uid_t owner, gid_t group);
extern uid_t getuid(void);
extern uid_t geteuid(void);
extern gid_t getgid(void);
extern gid_t getegid(void);
extern int seteuid(uid_t euid);
extern int setegid(gid_t egid);
extern int setuid(uid_t uid);
extern int setgid(gid_t gid);
extern int getgroups(int gidsetsize, gid_t grouplist[]);
extern char *getlogin(void);

/* Symbolic links / hard links */
extern ssize_t readlink(const char *path, char *buf, size_t bufsize);
extern int symlink(const char *target, const char *linkpath);
extern int link(const char *path1, const char *path2);

/* Process groups and sessions */
extern pid_t getpgid(pid_t pid);
extern int setpgid(pid_t pid, pid_t pgid);
extern pid_t getpgrp(void);
extern pid_t setsid(void);
extern pid_t getsid(pid_t pid);

/* Alarms and signal waiting */
extern unsigned int alarm(unsigned int seconds);
extern int pause(void);

/* Misc process/host */
extern int fchdir(int fd);
extern int gethostname(char *name, size_t namelen);
extern int sethostname(const char *name, size_t namelen);
extern int lchown(const char *path, uid_t owner, gid_t group);
extern int ttyname_r(int fd, char *buf, size_t buflen);
extern int getlogin_r(char *name, size_t namesize);
extern int setgroups(int ngroups, const gid_t *grouplist);
extern int initgroups(const char *user, gid_t group);
extern int nice(int incr);

/* Scatter/gather I/O */
struct iovec {
    void *iov_base;
    size_t iov_len;
};
extern ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
extern ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

/* Page size (legacy BSD interface used by some VFS layers) */
extern int getpagesize(void);

#ifdef __linux__
// splice: Linux-only zero-copy pipe I/O, same gap class as mremap/fallocate
// -- SQLite's unix VFS can reference it under Linux-specific config (#731).
extern ssize_t splice(int fd_in, off_t *off_in, int fd_out, off_t *off_out,
                      size_t len, unsigned int flags);
#endif

/* sysconf()/pathconf()/confstr() constants.
 *
 * These are CCCC's own canonical numbering -- NOT the host libc's numbering.
 * sysconf/pathconf/confstr are registered against translating wrapper
 * functions (wrap_sysconf/wrap_pathconf/wrap_fpathconf/wrap_confstr in
 * src/stdlib/posix.c) that map these canonical values to whatever the host
 * libc actually expects before calling through. This keeps compiled .c4
 * bytecode portable across hosts with different _SC_, _PC_, and _CS_
 * numbering (e.g. macOS vs glibc disagree on nearly all of these) and fixes
 * a latent
 * bug where _SC_NPROCESSORS_ONLN used to be hard-coded to the macOS value
 * and passed straight to the host sysconf() unconditionally.
 */

/* _SC_* -- sysconf() names */
#define _SC_ARG_MAX             1
#define _SC_CHILD_MAX           2
#define _SC_CLK_TCK             3
#define _SC_NGROUPS_MAX         4
#define _SC_OPEN_MAX            5
#define _SC_STREAM_MAX          6
#define _SC_TZNAME_MAX          7
#define _SC_JOB_CONTROL         8
#define _SC_SAVED_IDS           9
#define _SC_VERSION             10
#define _SC_PAGESIZE            11
#define _SC_PAGE_SIZE           _SC_PAGESIZE
#define _SC_NPROCESSORS_CONF    12
#define _SC_NPROCESSORS_ONLN    13
#define _SC_PHYS_PAGES          14
#define _SC_LINE_MAX            15
#define _SC_RE_DUP_MAX          16
#define _SC_2_VERSION           17
#define _SC_XOPEN_VERSION       18
#define _SC_HOST_NAME_MAX       19
#define _SC_LOGIN_NAME_MAX      20
#define _SC_TTY_NAME_MAX        21
#define _SC_SYMLOOP_MAX         22
#define _SC_ATEXIT_MAX          23
#define _SC_IOV_MAX             24
#define _SC_GETPW_R_SIZE_MAX    25
#define _SC_GETGR_R_SIZE_MAX    26
#define _SC_MONOTONIC_CLOCK     27

/* _PC_* -- pathconf()/fpathconf() names */
#define _PC_LINK_MAX            1
#define _PC_MAX_CANON           2
#define _PC_MAX_INPUT           3
#define _PC_NAME_MAX            4
#define _PC_PATH_MAX            5
#define _PC_PIPE_BUF            6
#define _PC_CHOWN_RESTRICTED    7
#define _PC_NO_TRUNC            8
#define _PC_VDISABLE            9

/* _CS_* -- confstr() names */
#define _CS_PATH                1

/* POSIX/X\/Open version this CCCC targets (VM-model constants, not derived
 * from the host). Kept in sync with the feature-test macros predefined by
 * the compiler (see init_macros in src/preprocess.c). */
#define _POSIX_VERSION   200809L
#define _POSIX2_VERSION  200809L
#define _XOPEN_VERSION   700

#endif /* __UNISTD_H */
