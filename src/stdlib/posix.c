// POSIX and dlfcn stdlib function registration (#946)
//
// This file is a thin aggregator: register_posix_functions() is the one
// externally-referenced entry point (looked up by name in
// src/preprocess.c's fns[] table), and it dispatches to each domain
// file's own register_posix_*_functions(). The domain files themselves
// (posix_io.c, posix_net.c, posix_poll.c, posix_wait.c, posix_dir.c,
// posix_search.c, posix_sched.c, posix_statfs.c, posix_ipc.c,
// posix_wordexp.c, posix_aio.c, posix_mqueue.c, posix_ndbm.c,
// posix_spawn.c, posix_lang.c) carry the actual wrapper implementations;
// posix_util.h/.c carries the handful of helpers shared across more than
// one of them (GIL save/release, guest/host sigset translation).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

void register_posix_io_functions(VirtualMachine *vm);
void register_posix_net_functions(VirtualMachine *vm);
void register_posix_poll_functions(VirtualMachine *vm);
void register_posix_wait_functions(VirtualMachine *vm);
void register_posix_dir_functions(VirtualMachine *vm);
void register_posix_search_functions(VirtualMachine *vm);
void register_posix_sched_functions(VirtualMachine *vm);
void register_posix_statfs_functions(VirtualMachine *vm);
void register_posix_ipc_functions(VirtualMachine *vm);
void register_posix_wordexp_functions(VirtualMachine *vm);
void register_posix_aio_functions(VirtualMachine *vm);
void register_posix_mqueue_functions(VirtualMachine *vm);
void register_posix_ndbm_functions(VirtualMachine *vm);
void register_posix_spawn_functions(VirtualMachine *vm);
void register_posix_lang_functions(VirtualMachine *vm);

void register_posix_functions(VirtualMachine *vm) {
    register_posix_io_functions(vm);
    register_posix_net_functions(vm);
    register_posix_poll_functions(vm);
    register_posix_wait_functions(vm);
    register_posix_dir_functions(vm);
    register_posix_search_functions(vm);
    register_posix_sched_functions(vm);
    register_posix_statfs_functions(vm);
    register_posix_ipc_functions(vm);
    register_posix_wordexp_functions(vm);
    register_posix_aio_functions(vm);
    register_posix_mqueue_functions(vm);
    register_posix_ndbm_functions(vm);
    register_posix_spawn_functions(vm);
    register_posix_lang_functions(vm);
}

#else
void register_posix_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
