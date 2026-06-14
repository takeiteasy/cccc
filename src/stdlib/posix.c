// POSIX and dlfcn stdlib function registration
#include "../cccc.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <glob.h>
#include <grp.h>
#include <libgen.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pwd.h>
#include <regex.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <utime.h>

static long long wrap_open(const char *path, long long oflag, ...) {
    mode_t mode = 0;
    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode = (mode_t)(unsigned int)va_arg(ap, unsigned int);
        va_end(ap);
    }
    return (long long)open(path, (int)oflag, mode);
}

static long long wrap_creat(const char *path, long long mode) {
    return (long long)creat(path, (mode_t)mode);
}

static long long wrap_close(long long fd) { return (long long)close((int)fd); }
static long long wrap_read(long long fd, long long buf, long long count) { return (long long)read((int)fd, (void *)buf, (size_t)count); }
static long long wrap_write(long long fd, long long buf, long long count) { return (long long)write((int)fd, (const void *)buf, (size_t)count); }
static long long wrap_lseek(long long fd, long long offset, long long whence) { return (long long)lseek((int)fd, (off_t)offset, (int)whence); }
static long long wrap_access(long long path, long long mode) { return (long long)access((const char *)path, (int)mode); }
static long long wrap_unlink(long long path) { return (long long)unlink((const char *)path); }
static long long wrap_rmdir(long long path) { return (long long)rmdir((const char *)path); }
static long long wrap_chdir(long long path) { return (long long)chdir((const char *)path); }
static long long wrap_usleep(long long usec) { return (long long)usleep((useconds_t)usec); }
static long long wrap_fork(void) { return (long long)fork(); }
static long long wrap_pipe(long long fd) { return (long long)pipe((int *)fd); }
static long long wrap__exit(long long status) { _exit((int)status); return 0; }

static long long wrap_umask(long long cmask) { return (long long)umask((mode_t)cmask); }
static long long wrap_htonl(long long hostlong) { return (long long)htonl((uint32_t)hostlong); }
static long long wrap_htons(long long hostshort) { return (long long)htons((uint16_t)hostshort); }
static long long wrap_ntohl(long long netlong) { return (long long)ntohl((uint32_t)netlong); }
static long long wrap_ntohs(long long netshort) { return (long long)ntohs((uint16_t)netshort); }
static long long wrap_inet_addr(long long cp) { return (long long)inet_addr((const char *)cp); }
static long long wrap_basename(long long path) { return (long long)basename((char *)path); }
static long long wrap_dirname(long long path) { return (long long)dirname((char *)path); }
static long long wrap_bzero(long long s, long long n) { bzero((void *)s, (size_t)n); return 0; }
static long long wrap_bcopy(long long src, long long dst, long long n) { bcopy((const void *)src, (void *)dst, (size_t)n); return 0; }
static long long wrap_freeaddrinfo(long long res) { freeaddrinfo((struct addrinfo *)res); return 0; }
static long long wrap_globfree(long long pglob) { globfree((glob_t *)pglob); return 0; }
static long long wrap_regfree(long long preg) { regfree((regex_t *)preg); return 0; }

void register_posix_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "read", (void*)wrap_read, 3, 0);
    cc_register_cfunc(vm, "write", (void*)wrap_write, 3, 0);
    cc_register_cfunc(vm, "close", (void*)wrap_close, 1, 0);
    cc_register_cfunc(vm, "lseek", (void*)wrap_lseek, 3, 0);
    cc_register_cfunc(vm, "access", (void*)wrap_access, 2, 0);
    cc_register_cfunc(vm, "unlink", (void*)wrap_unlink, 1, 0);
    cc_register_cfunc(vm, "rmdir", (void*)wrap_rmdir, 1, 0);
    cc_register_cfunc(vm, "chdir", (void*)wrap_chdir, 1, 0);
    cc_register_cfunc(vm, "getcwd", (void*)getcwd, 2, 0);
    cc_register_cfunc(vm, "getpid", (void*)getpid, 0, 0);
    cc_register_cfunc(vm, "getppid", (void*)getppid, 0, 0);
    cc_register_cfunc(vm, "sleep", (void*)sleep, 1, 0);
    cc_register_cfunc(vm, "usleep", (void*)wrap_usleep, 1, 0);
    cc_register_cfunc(vm, "fork", (void*)wrap_fork, 0, 0);
    cc_register_cfunc(vm, "pipe", (void*)wrap_pipe, 1, 0);
    cc_register_cfunc(vm, "_exit", (void*)wrap__exit, 1, 0);
    cc_register_cfunc(vm, "execv", (void*)execv, 2, 0);
    cc_register_cfunc(vm, "execve", (void*)execve, 3, 0);
    cc_register_variadic_cfunc(vm, "execl", (void*)execl, 2, 0);
    cc_register_variadic_cfunc(vm, "execlp", (void*)execlp, 2, 0);
    cc_register_variadic_cfunc(vm, "execle", (void*)execle, 2, 0);
    cc_register_cfunc(vm, "execvp", (void*)execvp, 2, 0);
    cc_register_variadic_cfunc(vm, "open", (void*)wrap_open, 2, 0);
    cc_register_cfunc(vm, "creat", (void*)wrap_creat, 2, 0);
    cc_register_variadic_cfunc(vm, "fcntl", (void*)fcntl, 2, 0);

    cc_register_cfunc(vm, "strcasecmp", (void*)strcasecmp, 2, 0);
    cc_register_cfunc(vm, "strncasecmp", (void*)strncasecmp, 3, 0);
    cc_register_cfunc(vm, "bcmp", (void*)bcmp, 3, 0);
    cc_register_cfunc(vm, "index", (void*)index, 2, 0);
    cc_register_cfunc(vm, "rindex", (void*)rindex, 2, 0);
    cc_register_cfunc(vm, "basename", (void*)wrap_basename, 1, 0);
    cc_register_cfunc(vm, "dirname", (void*)wrap_dirname, 1, 0);
    cc_register_cfunc(vm, "fnmatch", (void*)fnmatch, 3, 0);
    cc_register_cfunc(vm, "getopt", (void*)getopt, 3, 0);
    cc_register_cfunc(vm, "getopt_long", (void*)getopt_long, 5, 0);
    cc_register_cfunc(vm, "poll", (void*)poll, 3, 0);
    cc_register_cfunc(vm, "wait", (void*)wait, 1, 0);
    cc_register_cfunc(vm, "waitpid", (void*)waitpid, 3, 0);
    cc_register_cfunc(vm, "gettimeofday", (void*)gettimeofday, 2, 0);
    cc_register_cfunc(vm, "settimeofday", (void*)settimeofday, 2, 0);
    cc_register_cfunc(vm, "mmap", (void*)mmap, 6, 0);
    cc_register_cfunc(vm, "munmap", (void*)munmap, 2, 0);
    cc_register_cfunc(vm, "mprotect", (void*)mprotect, 3, 0);
    cc_register_cfunc(vm, "msync", (void*)msync, 3, 0);
    cc_register_cfunc(vm, "posix_madvise", (void*)posix_madvise, 3, 0);
    cc_register_cfunc(vm, "stat", (void*)stat, 2, 0);
    cc_register_cfunc(vm, "fstat", (void*)fstat, 2, 0);
    cc_register_cfunc(vm, "lstat", (void*)lstat, 2, 0);
    cc_register_cfunc(vm, "chmod", (void*)chmod, 2, 0);
    cc_register_cfunc(vm, "mkdir", (void*)mkdir, 2, 0);
    cc_register_cfunc(vm, "mkfifo", (void*)mkfifo, 2, 0);
    cc_register_cfunc(vm, "umask", (void*)wrap_umask, 1, 0);
    cc_register_cfunc(vm, "utime", (void*)utime, 2, 0);
    cc_register_cfunc(vm, "htonl", (void*)wrap_htonl, 1, 0);
    cc_register_cfunc(vm, "htons", (void*)wrap_htons, 1, 0);
    cc_register_cfunc(vm, "ntohl", (void*)wrap_ntohl, 1, 0);
    cc_register_cfunc(vm, "ntohs", (void*)wrap_ntohs, 1, 0);
    cc_register_cfunc(vm, "inet_addr", (void*)wrap_inet_addr, 1, 0);
    cc_register_cfunc(vm, "inet_ntoa", (void*)inet_ntoa, 1, 0);
    cc_register_cfunc(vm, "inet_ntop", (void*)inet_ntop, 4, 0);
    cc_register_cfunc(vm, "inet_pton", (void*)inet_pton, 3, 0);
    cc_register_cfunc(vm, "bzero", (void*)wrap_bzero, 2, 0);
    cc_register_cfunc(vm, "bcopy", (void*)wrap_bcopy, 3, 0);

    cc_register_cfunc(vm, "socket", (void*)socket, 3, 0);
    cc_register_cfunc(vm, "bind", (void*)bind, 3, 0);
    cc_register_cfunc(vm, "listen", (void*)listen, 2, 0);
    cc_register_cfunc(vm, "accept", (void*)accept, 3, 0);
    cc_register_cfunc(vm, "connect", (void*)connect, 3, 0);
    cc_register_cfunc(vm, "setsockopt", (void*)setsockopt, 5, 0);
    cc_register_cfunc(vm, "getsockname", (void*)getsockname, 3, 0);
    cc_register_cfunc(vm, "shutdown", (void*)shutdown, 2, 0);
    cc_register_cfunc(vm, "gethostbyname", (void*)gethostbyname, 1, 0);
    cc_register_cfunc(vm, "getaddrinfo", (void*)getaddrinfo, 4, 0);
    cc_register_cfunc(vm, "freeaddrinfo", (void*)wrap_freeaddrinfo, 1, 0);

    cc_register_cfunc(vm, "opendir", (void*)opendir, 1, 0);
    cc_register_cfunc(vm, "readdir", (void*)readdir, 1, 0);
    cc_register_cfunc(vm, "closedir", (void*)closedir, 1, 0);
    cc_register_cfunc(vm, "tcgetattr", (void*)tcgetattr, 2, 0);
    cc_register_cfunc(vm, "tcsetattr", (void*)tcsetattr, 3, 0);
    cc_register_cfunc(vm, "getpwuid", (void*)getpwuid, 1, 0);
    cc_register_cfunc(vm, "getpwnam", (void*)getpwnam, 1, 0);
    cc_register_cfunc(vm, "getgrgid", (void*)getgrgid, 1, 0);
    cc_register_cfunc(vm, "getgrnam", (void*)getgrnam, 1, 0);
    cc_register_cfunc(vm, "regcomp", (void*)regcomp, 3, 0);
    cc_register_cfunc(vm, "regexec", (void*)regexec, 5, 0);
    cc_register_cfunc(vm, "regerror", (void*)regerror, 4, 0);
    cc_register_cfunc(vm, "regfree", (void*)wrap_regfree, 1, 0);
    cc_register_cfunc(vm, "glob", (void*)glob, 4, 0);
    cc_register_cfunc(vm, "globfree", (void*)wrap_globfree, 1, 0);
}
#else
void register_posix_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
