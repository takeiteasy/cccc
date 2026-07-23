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

#ifdef __APPLE__
#define _SC_PAGESIZE 29
#else
#define _SC_PAGESIZE 30
#endif
#define _SC_PAGE_SIZE _SC_PAGESIZE

#define _SC_NPROCESSORS_ONLN 58

#endif /* __UNISTD_H */
