// posix_spawn.c -- spawn.h (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

// spawn.h (#801) -- posix_spawnattr_t/posix_spawn_file_actions_t are
// opaque pointer-width handles on the guest (see include/spawn.h). *_init
// mallocs a host-sized object (sizeof(posix_spawnattr_t) on the host --
// 336 bytes on glibc, 8 on macOS where the type itself is already a void*
// the kernel manages internally), calls the real host *_init on it, and
// stores that host pointer through the guest's pointer-sized handle;
// *_destroy calls the host destroy then frees it. Every other wrapper
// dereferences the guest handle once to recover the host object pointer
// before calling the corresponding host function.
static long long wrap_posix_spawnattr_init(long long attr_ptr) {
    posix_spawnattr_t *host_attr = malloc(sizeof(posix_spawnattr_t));
    int rc = posix_spawnattr_init(host_attr);
    *(void **)(void *)attr_ptr = (void *)host_attr;
    return rc;
}

static long long wrap_posix_spawnattr_destroy(long long attr_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    int rc = posix_spawnattr_destroy((posix_spawnattr_t *)host_attr);
    free(host_attr);
    return rc;
}

static long long wrap_posix_spawnattr_getflags(long long attr_ptr, long long flags_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    short f = 0;
    int rc = posix_spawnattr_getflags((posix_spawnattr_t *)host_attr, &f);
    if (rc == 0 && flags_ptr) *(short *)(void *)flags_ptr = f;
    return rc;
}

static long long wrap_posix_spawnattr_setflags(long long attr_ptr, long long flags) {
    void *host_attr = *(void **)(void *)attr_ptr;
    return posix_spawnattr_setflags((posix_spawnattr_t *)host_attr, (short)flags);
}

static long long wrap_posix_spawnattr_getpgroup(long long attr_ptr, long long pgroup_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    pid_t p = 0;
    int rc = posix_spawnattr_getpgroup((posix_spawnattr_t *)host_attr, &p);
    if (rc == 0 && pgroup_ptr) *(pid_t *)(void *)pgroup_ptr = p;
    return rc;
}

static long long wrap_posix_spawnattr_setpgroup(long long attr_ptr, long long pgroup) {
    void *host_attr = *(void **)(void *)attr_ptr;
    return posix_spawnattr_setpgroup((posix_spawnattr_t *)host_attr, (pid_t)pgroup);
}

// setsigdefault/setsigmask/getsigdefault/getsigmask translate CCCC's own
// 4-byte guest sigset_t to/from a real host sigset_t, the same conversion
// pselect() uses (guest_sigset_to_host, defined above) plus its reverse.
static unsigned int host_sigset_to_guest(const sigset_t *host_set) {
    unsigned int mask = 0;
    for (int signo = 1; signo < CCCC_NSIG; signo++) {
        if (sigismember(host_set, signo))
            mask |= (1u << (unsigned)(signo - 1));
    }
    return mask;
}

static long long wrap_posix_spawnattr_getsigdefault(long long attr_ptr, long long guest_sigset_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    sigset_t host_set;
    int rc = posix_spawnattr_getsigdefault((posix_spawnattr_t *)host_attr, &host_set);
    if (rc == 0 && guest_sigset_ptr)
        *(unsigned int *)(void *)guest_sigset_ptr = host_sigset_to_guest(&host_set);
    return rc;
}

static long long wrap_posix_spawnattr_setsigdefault(long long attr_ptr, long long guest_sigset_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    sigset_t host_set;
    cccc_posix_guest_sigset_to_host(*(unsigned int *)(void *)guest_sigset_ptr, &host_set);
    return posix_spawnattr_setsigdefault((posix_spawnattr_t *)host_attr, &host_set);
}

static long long wrap_posix_spawnattr_getsigmask(long long attr_ptr, long long guest_sigset_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    sigset_t host_set;
    int rc = posix_spawnattr_getsigmask((posix_spawnattr_t *)host_attr, &host_set);
    if (rc == 0 && guest_sigset_ptr)
        *(unsigned int *)(void *)guest_sigset_ptr = host_sigset_to_guest(&host_set);
    return rc;
}

static long long wrap_posix_spawnattr_setsigmask(long long attr_ptr, long long guest_sigset_ptr) {
    void *host_attr = *(void **)(void *)attr_ptr;
    sigset_t host_set;
    cccc_posix_guest_sigset_to_host(*(unsigned int *)(void *)guest_sigset_ptr, &host_set);
    return posix_spawnattr_setsigmask((posix_spawnattr_t *)host_attr, &host_set);
}

static long long wrap_posix_spawn_file_actions_init(long long fa_ptr) {
    posix_spawn_file_actions_t *host_fa = malloc(sizeof(posix_spawn_file_actions_t));
    int rc = posix_spawn_file_actions_init(host_fa);
    *(void **)(void *)fa_ptr = (void *)host_fa;
    return rc;
}

static long long wrap_posix_spawn_file_actions_destroy(long long fa_ptr) {
    void *host_fa = *(void **)(void *)fa_ptr;
    int rc = posix_spawn_file_actions_destroy((posix_spawn_file_actions_t *)host_fa);
    free(host_fa);
    return rc;
}

static long long wrap_posix_spawn_file_actions_addopen(long long fa_ptr, long long fildes,
                                                        long long path, long long oflag,
                                                        long long mode) {
    void *host_fa = *(void **)(void *)fa_ptr;
    return posix_spawn_file_actions_addopen((posix_spawn_file_actions_t *)host_fa, (int)fildes,
                                             (const char *)path, (int)oflag, (mode_t)mode);
}

static long long wrap_posix_spawn_file_actions_addclose(long long fa_ptr, long long fildes) {
    void *host_fa = *(void **)(void *)fa_ptr;
    return posix_spawn_file_actions_addclose((posix_spawn_file_actions_t *)host_fa, (int)fildes);
}

static long long wrap_posix_spawn_file_actions_adddup2(long long fa_ptr, long long fildes,
                                                        long long newfildes) {
    void *host_fa = *(void **)(void *)fa_ptr;
    return posix_spawn_file_actions_adddup2((posix_spawn_file_actions_t *)host_fa, (int)fildes,
                                             (int)newfildes);
}

// posix_spawn()/posix_spawnp() fork+exec, so they release the GIL like the
// other blocking wrappers above. argv/envp are guest arrays of guest
// char *, which are already host addresses in CCCC's flat address space --
// same passthrough execv/execve/execvp already rely on, no marshaling
// needed.
static long long wrap_posix_spawn_gil(long long pid_ptr, long long path, long long file_actions_ptr,
                                      long long attrp_ptr, long long argv, long long envp) {
    void *host_fa = file_actions_ptr ? *(void **)(void *)file_actions_ptr : NULL;
    void *host_attr = attrp_ptr ? *(void **)(void *)attrp_ptr : NULL;
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)posix_spawn((pid_t *)pid_ptr, (const char *)path,
                                       (const posix_spawn_file_actions_t *)host_fa,
                                       (const posix_spawnattr_t *)host_attr,
                                       (char *const *)argv, (char *const *)envp);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = posix_spawn((pid_t *)pid_ptr, (const char *)path,
                         (const posix_spawn_file_actions_t *)host_fa,
                         (const posix_spawnattr_t *)host_attr,
                         (char *const *)argv, (char *const *)envp);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_posix_spawnp_gil(long long pid_ptr, long long file, long long file_actions_ptr,
                                       long long attrp_ptr, long long argv, long long envp) {
    void *host_fa = file_actions_ptr ? *(void **)(void *)file_actions_ptr : NULL;
    void *host_attr = attrp_ptr ? *(void **)(void *)attrp_ptr : NULL;
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)posix_spawnp((pid_t *)pid_ptr, (const char *)file,
                                        (const posix_spawn_file_actions_t *)host_fa,
                                        (const posix_spawnattr_t *)host_attr,
                                        (char *const *)argv, (char *const *)envp);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = posix_spawnp((pid_t *)pid_ptr, (const char *)file,
                          (const posix_spawn_file_actions_t *)host_fa,
                          (const posix_spawnattr_t *)host_attr,
                          (char *const *)argv, (char *const *)envp);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

void register_posix_spawn_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "posix_spawnattr_init", (void*)wrap_posix_spawnattr_init, 1, 0);
    cc_register_cfunc(vm, "posix_spawnattr_destroy", (void*)wrap_posix_spawnattr_destroy, 1, 0);
    cc_register_cfunc(vm, "posix_spawnattr_getflags", (void*)wrap_posix_spawnattr_getflags, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_setflags", (void*)wrap_posix_spawnattr_setflags, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_getpgroup", (void*)wrap_posix_spawnattr_getpgroup, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_setpgroup", (void*)wrap_posix_spawnattr_setpgroup, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_getsigdefault", (void*)wrap_posix_spawnattr_getsigdefault, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_setsigdefault", (void*)wrap_posix_spawnattr_setsigdefault, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_getsigmask", (void*)wrap_posix_spawnattr_getsigmask, 2, 0);
    cc_register_cfunc(vm, "posix_spawnattr_setsigmask", (void*)wrap_posix_spawnattr_setsigmask, 2, 0);
    cc_register_cfunc(vm, "posix_spawn_file_actions_init", (void*)wrap_posix_spawn_file_actions_init, 1, 0);
    cc_register_cfunc(vm, "posix_spawn_file_actions_destroy", (void*)wrap_posix_spawn_file_actions_destroy, 1, 0);
    cc_register_cfunc(vm, "posix_spawn_file_actions_addopen", (void*)wrap_posix_spawn_file_actions_addopen, 5, 0);
    cc_register_cfunc(vm, "posix_spawn_file_actions_addclose", (void*)wrap_posix_spawn_file_actions_addclose, 2, 0);
    cc_register_cfunc(vm, "posix_spawn_file_actions_adddup2", (void*)wrap_posix_spawn_file_actions_adddup2, 3, 0);
    cc_register_cfunc(vm, "posix_spawn", (void*)wrap_posix_spawn_gil, 6, 0);
    cc_register_cfunc(vm, "posix_spawnp", (void*)wrap_posix_spawnp_gil, 6, 0);
}

#else
void register_posix_spawn_functions(VirtualMachine *vm) { (void)vm; }
#endif
