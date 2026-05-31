// POSIX and dlfcn stdlib function registration
#include "../jcc.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

static long long wrap_open(const char *path, long long oflag, long long mode) {
    return (long long)open(path, (int)oflag, (mode_t)mode);
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

void register_posix_functions(JCC *vm) {
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
    cc_register_cfunc(vm, "execv", (void*)execv, 2, 0);
    cc_register_cfunc(vm, "execve", (void*)execve, 3, 0);
    cc_register_variadic_cfunc(vm, "execl", (void*)execl, 2, 0);
    cc_register_variadic_cfunc(vm, "execlp", (void*)execlp, 2, 0);
    cc_register_variadic_cfunc(vm, "execle", (void*)execle, 2, 0);
    cc_register_cfunc(vm, "execvp", (void*)execvp, 2, 0);
    cc_register_variadic_cfunc(vm, "open", (void*)wrap_open, 2, 0);
    cc_register_cfunc(vm, "creat", (void*)wrap_creat, 2, 0);
    cc_register_variadic_cfunc(vm, "fcntl", (void*)fcntl, 2, 0);

    cc_register_cfunc(vm, "dlopen", (void*)dlopen, 2, 0);
    cc_register_cfunc(vm, "dlsym", (void*)dlsym, 2, 0);
    cc_register_cfunc(vm, "dlclose", (void*)dlclose, 1, 0);
    cc_register_cfunc(vm, "dlerror", (void*)dlerror, 0, 0);
}
#else
void register_posix_functions(JCC *vm) {
    (void)vm;
}
#endif
