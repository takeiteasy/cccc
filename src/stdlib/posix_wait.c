// posix_wait.c -- process-wait and sleep wrappers (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

#if defined(CCCC_LEAK_PASS) && defined(__APPLE__)
// The leak-pass build (build.c test_leaks): under `leaks -atExit` a child the
// guest forks stops itself at exit and the guest's wait would block forever.
// Continue such a child unless the guest asked to see stops (WUNTRACED).
static pid_t resume_wait4(pid_t pid, int *status, int options,
                          struct rusage *ru) {
    if (options & WUNTRACED)
        return wait4(pid, status, options, ru);
    for (;;) {
        int   st = 0;
        pid_t r  = wait4(pid, &st, options | WUNTRACED, ru);
        if (r <= 0)
            return r;
        if (WIFSTOPPED(st)) {
            kill(r, SIGCONT);
            continue;
        }
        if (status)
            *status = st;
        return r;
    }
}

static int resume_waitid(idtype_t idtype, id_t id, siginfo_t *info,
                         int options) {
    if (options & WSTOPPED)
        return waitid(idtype, id, info, options);
    siginfo_t local;
    siginfo_t *si = info ? info : &local;
    for (;;) {
        si->si_pid = 0;
        int r      = waitid(idtype, id, si, options | WSTOPPED);
        if (r < 0 || si->si_pid == 0 || si->si_code != CLD_STOPPED)
            return r;
        kill(si->si_pid, SIGCONT);
    }
}

#define host_wait(st) resume_wait4(-1, st, 0, NULL)
#define host_waitpid(pid, st, opts) resume_wait4(pid, st, opts, NULL)
#define host_wait3(st, opts, ru) resume_wait4(-1, st, opts, ru)
#define host_wait4 resume_wait4
#define host_waitid resume_waitid
#else
#define host_wait wait
#define host_waitpid waitpid
#define host_wait3 wait3
#define host_wait4 wait4
#define host_waitid waitid
#endif

static long long wrap_wait_gil(long long wstatus) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)host_wait((int *)wstatus);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    pid_t r = host_wait((int *)wstatus);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_waitpid_gil(long long pid, long long wstatus,
                                  long long options) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)host_waitpid((pid_t)pid, (int *)wstatus, (int)options);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    pid_t r = host_waitpid((pid_t)pid, (int *)wstatus, (int)options);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_waitid_gil(long long idtype, long long id,
                                 long long infop, long long options) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)host_waitid((idtype_t)idtype, (id_t)id, (siginfo_t *)infop,
                                 (int)options);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r =
        host_waitid((idtype_t)idtype, (id_t)id, (siginfo_t *)infop, (int)options);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_wait3_gil(long long wstatus, long long options,
                                long long rusage) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)host_wait3((int *)wstatus, (int)options,
                                (struct rusage *)rusage);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    pid_t r = host_wait3((int *)wstatus, (int)options, (struct rusage *)rusage);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_wait4_gil(long long pid, long long wstatus,
                                long long options, long long rusage) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)host_wait4((pid_t)pid, (int *)wstatus, (int)options,
                                (struct rusage *)rusage);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    pid_t r = host_wait4((pid_t)pid, (int *)wstatus, (int)options,
                    (struct rusage *)rusage);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_sleep_gil(long long seconds) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)sleep((unsigned int)seconds);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    unsigned int r = sleep((unsigned int)seconds);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_usleep_gil(long long usec) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)usleep((useconds_t)usec);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = usleep((useconds_t)usec);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_pause_gil(void) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)pause();
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = pause();
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

void register_posix_wait_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "wait", (void *)wrap_wait_gil, 1, 0);
    cc_register_cfunc(vm, "waitpid", (void *)wrap_waitpid_gil, 3, 0);
    cc_register_cfunc(vm, "waitid", (void *)wrap_waitid_gil, 4, 0);
    cc_register_cfunc(vm, "wait3", (void *)wrap_wait3_gil, 3, 0);
    cc_register_cfunc(vm, "wait4", (void *)wrap_wait4_gil, 4, 0);
    cc_register_cfunc(vm, "sleep", (void *)wrap_sleep_gil, 1, 0);
    cc_register_cfunc(vm, "usleep", (void *)wrap_usleep_gil, 1, 0);
    cc_register_cfunc(vm, "pause", (void *)wrap_pause_gil, 0, 0);
}

#else
void register_posix_wait_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
