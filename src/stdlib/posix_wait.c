// posix_wait.c -- process-wait and sleep wrappers (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

static long long wrap_wait_gil(long long wstatus) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)wait((int *)wstatus);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    pid_t r = wait((int *)wstatus);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_waitpid_gil(long long pid, long long wstatus, long long options) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)waitpid((pid_t)pid, (int *)wstatus, (int)options);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    pid_t r = waitpid((pid_t)pid, (int *)wstatus, (int)options);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_waitid_gil(long long idtype, long long id, long long infop, long long options) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)waitid((idtype_t)idtype, (id_t)id, (siginfo_t *)infop, (int)options);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = waitid((idtype_t)idtype, (id_t)id, (siginfo_t *)infop, (int)options);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_wait3_gil(long long wstatus, long long options, long long rusage) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)wait3((int *)wstatus, (int)options, (struct rusage *)rusage);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    pid_t r = wait3((int *)wstatus, (int)options, (struct rusage *)rusage);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_wait4_gil(long long pid, long long wstatus, long long options, long long rusage) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)wait4((pid_t)pid, (int *)wstatus, (int)options, (struct rusage *)rusage);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    pid_t r = wait4((pid_t)pid, (int *)wstatus, (int)options, (struct rusage *)rusage);
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
    cc_register_cfunc(vm, "wait",    (void*)wrap_wait_gil,    1, 0);
    cc_register_cfunc(vm, "waitpid", (void*)wrap_waitpid_gil, 3, 0);
    cc_register_cfunc(vm, "waitid",  (void*)wrap_waitid_gil,  4, 0);
    cc_register_cfunc(vm, "wait3",   (void*)wrap_wait3_gil,   3, 0);
    cc_register_cfunc(vm, "wait4",   (void*)wrap_wait4_gil,   4, 0);
    cc_register_cfunc(vm, "sleep",   (void*)wrap_sleep_gil,   1, 0);
    cc_register_cfunc(vm, "usleep",  (void*)wrap_usleep_gil,  1, 0);
    cc_register_cfunc(vm, "pause",   (void*)wrap_pause_gil,   0, 0);
}

#else
void register_posix_wait_functions(VirtualMachine *vm) { (void)vm; }
#endif
