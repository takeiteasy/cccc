// Shared POSIX stdlib helpers (#946) -- see posix_util.h for scope.
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

VirtualMachine *cccc_posix_current_vm(void) {
    return cccc_current_ffi_vm();
}

void cccc_posix_save_and_release_gil(VirtualMachine *vm, ExecState *state) {
    cccc_exec_state_save(vm, state);
    cccc_gil_release(vm);
}

void cccc_posix_acquire_and_restore_gil(VirtualMachine *vm, const ExecState *state) {
    cccc_gil_acquire(vm);
    cccc_exec_state_restore(vm, state);
}

void cccc_posix_guest_sigset_to_host(unsigned int guest_mask, sigset_t *host_set) {
    sigemptyset(host_set);
    for (int signo = 1; signo < CCCC_NSIG; signo++) {
        if (guest_mask & (1u << (unsigned)(signo - 1)))
            sigaddset(host_set, signo);
    }
}

#else
// Keeps this TU non-empty under a Windows build, matching the register_*
// stub every other posix_*.c file carries in its own #else arm.
typedef int cccc_posix_util_unused_tu;
#endif /* !_WIN32 && !_WIN64 */
