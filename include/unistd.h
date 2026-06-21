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
extern int ftruncate(int fd, off_t length);
extern int truncate(const char *path, off_t length);
extern long sysconf(int name);
extern int mkstemp(char *tmpl);
extern char *mkdtemp(char *tmpl);

#define _SC_NPROCESSORS_ONLN 58

#endif /* __UNISTD_H */
