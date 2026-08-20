// posix_wordexp.c -- wordexp.h (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

// wordexp.h (#802) -- wordexp_t is byte-identical on macOS and glibc
// ({ size_t we_wordc; char **we_wordv; size_t we_offs; }, verified against
// real headers), so it's a plain pass-through; only the WRDE_* constants
// diverge (include/wordexp.h splits those, same pattern as glob.h's
// GLOB_*). wordexp() forks a shell, so it releases the GIL like the other
// blocking wrappers.
static long long wrap_wordexp(long long words, long long we, long long flags) {
    VirtualMachine *vm = cccc_posix_current_vm();
    if (!vm || !vm->gil_initialized)
        return (long long)wordexp((const char *)words, (wordexp_t *)(void *)we,
                                  (int)flags);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = wordexp((const char *)words, (wordexp_t *)(void *)we, (int)flags);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_wordfree(long long we) {
    wordfree((wordexp_t *)(void *)we);
    return 0;
}

void register_posix_wordexp_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "wordexp", (void *)wrap_wordexp, 3, 0);
    cc_register_cfunc(vm, "wordfree", (void *)wrap_wordfree, 1, 0);
}

#else
void register_posix_wordexp_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
