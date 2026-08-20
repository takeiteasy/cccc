// posix_dir.c -- dirent/glob/scandir/fts, plus the regex-family
// pass-through registrations that travel with glob (#946 split of
// posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

static long long wrap_globfree(long long pglob) {
    globfree((glob_t *)pglob);
    return 0;
}
static long long wrap_regfree(long long preg) {
    regfree((regex_t *)preg);
    return 0;
}

// ---------------------------------------------------------------------------
// glob()'s errfunc / scandir()'s select+compar callbacks (#738)
//
// Same shape as qsort/bsearch's comparator fix above: a thread-local slot
// holds which guest callback to invoke, a real host C trampoline reads it
// and drives cccc_call_guest_callback, and the slot is saved/restored around
// the host call (not written once) so a guest callback that itself calls
// glob()/scandir() doesn't clobber the outer call's slot. Faults are latched
// and surfaced via errno after the host call returns, since none of these
// host APIs give the callback its own error-propagation channel.
// ---------------------------------------------------------------------------

static _Thread_local long long g_glob_errfunc_value;
static _Thread_local int       g_glob_errfunc_faulted;

static int glob_errfunc_trampoline(const char *epath, int eerrno) {
    VirtualMachine *vm      = cccc_current_ffi_vm();
    long long       args[2] = {(long long)(intptr_t)epath, (long long)eerrno};
    long long       result  = 0;
    if (!vm || cccc_call_guest_callback(vm, g_glob_errfunc_value, args, 2,
                                        &result) != 0) {
        g_glob_errfunc_faulted = 1;
        return 0; /* keep glob() enumerating rather than abort on a faulting
                     errfunc */
    }
    return (int)result;
}

static long long wrap_glob(long long pattern, long long flags,
                           long long errfunc, long long pglob) {
    long long saved_errfunc = g_glob_errfunc_value;
    int       saved_faulted = g_glob_errfunc_faulted;
    g_glob_errfunc_value    = errfunc;
    g_glob_errfunc_faulted  = 0;

    int r = glob((const char *)pattern, (int)flags,
                 errfunc ? glob_errfunc_trampoline : NULL, (glob_t *)pglob);

    int faulted            = g_glob_errfunc_faulted;
    g_glob_errfunc_value   = saved_errfunc;
    g_glob_errfunc_faulted = saved_faulted;
    if (faulted)
        errno = EFAULT;
    return r;
}

static _Thread_local long long g_scandir_select_value;
static _Thread_local long long g_scandir_compar_value;
static _Thread_local int       g_scandir_faulted;

static int scandir_select_trampoline(const struct dirent *e) {
    VirtualMachine *vm      = cccc_current_ffi_vm();
    long long       args[1] = {(long long)(intptr_t)e};
    long long       result  = 0;
    if (!vm || cccc_call_guest_callback(vm, g_scandir_select_value, args, 1,
                                        &result) != 0) {
        g_scandir_faulted = 1;
        return 0;
    }
    return (int)result;
}

static int scandir_compar_trampoline(const struct dirent **a,
                                     const struct dirent **b) {
    VirtualMachine *vm      = cccc_current_ffi_vm();
    long long       args[2] = {(long long)(intptr_t)a, (long long)(intptr_t)b};
    long long       result  = 0;
    if (!vm || cccc_call_guest_callback(vm, g_scandir_compar_value, args, 2,
                                        &result) != 0) {
        g_scandir_faulted = 1;
        return 0;
    }
    return (int)result;
}

static long long wrap_scandir(long long dirname, long long namelist,
                              long long select, long long compar) {
    long long saved_select  = g_scandir_select_value;
    long long saved_compar  = g_scandir_compar_value;
    int       saved_faulted = g_scandir_faulted;
    g_scandir_select_value  = select;
    g_scandir_compar_value  = compar;
    g_scandir_faulted       = 0;

    int n       = scandir((const char *)dirname, (struct dirent ***)namelist,
                          select ? scandir_select_trampoline : NULL,
                          compar ? scandir_compar_trampoline : NULL);

    int faulted = g_scandir_faulted;
    g_scandir_select_value = saved_select;
    g_scandir_compar_value = saved_compar;
    g_scandir_faulted      = saved_faulted;
    if (faulted)
        errno = EFAULT;
    return n;
}

// fts.h (#811) -- fts_open()'s comparator takes the same
// int (*)(const FTSENT **, const FTSENT **) shape as scandir()'s compar
// above, so it gets the same thread-local-slot + trampoline treatment for
// the duration of any single host call. Unlike scandir()/glob(), though,
// the comparator isn't only invoked inside fts_open() itself -- libc
// retains it on the FTS handle and calls it again from every fts_read()
// (sorting each directory's children as it descends), so the slot has to
// be re-armed on every wrapper call that might invoke it, not just at
// fts_open() time (#878: fts_read() was calling the trampoline with the
// slot back at its default 0 value, which cccc_call_guest_callback
// resolved as a jump to byte offset 0 -- the unknown-opcode trap at the
// top of the text segment -- so the comparator silently never ran).
//
// The handle -> comparator binding lives in a small fixed-size table
// (guarded by the GIL, not thread-local -- one VM, one GIL, no need for
// per-thread copies) keyed on the FTS* pointer, populated by wrap_fts_open
// on success and cleared by wrap_fts_close.
//
// fts_open()/fts_set()/fts_close() don't block meaningfully (fts_open()
// itself may stat the root paths, but that's a single bounded syscall, not
// an unbounded wait) so they keep the GIL, matching opendir()'s category
// above. fts_read()/fts_children() do real directory I/O and normally
// release the GIL like the other blocking wrappers -- but only when the
// handle has no guest comparator: a comparator firing mid-fts_read() needs
// cccc_call_guest_callback to reenter vm_eval on this same thread, which
// requires the GIL, and would otherwise race the ExecState snapshot that
// posix_save_and_release_gil parked for restoration. So a handle with a
// comparator holds the GIL across fts_read()/fts_children() for its whole
// traversal instead.
static _Thread_local long long g_fts_compar_value;
static _Thread_local int       g_fts_faulted;

#define CCCC_FTS_HANDLE_MAX 16
typedef struct {
    FTS      *handle;
    long long compar;
} FtsHandleBinding;
static FtsHandleBinding g_fts_handles[CCCC_FTS_HANDLE_MAX];

static long long fts_handle_compar(FTS *f) {
    for (int i = 0; i < CCCC_FTS_HANDLE_MAX; i++)
        if (g_fts_handles[i].handle == f)
            return g_fts_handles[i].compar;
    return 0;
}

static void fts_handle_bind(FTS *f, long long compar) {
    for (int i = 0; i < CCCC_FTS_HANDLE_MAX; i++) {
        if (!g_fts_handles[i].handle) {
            g_fts_handles[i].handle = f;
            g_fts_handles[i].compar = compar;
            return;
        }
    }
    // Table full: comparator won't be retained past fts_open(). Rare
    // (16 concurrent guest traversals) and fails safe -- unsorted
    // children rather than a crash.
}

static void fts_handle_unbind(FTS *f) {
    for (int i = 0; i < CCCC_FTS_HANDLE_MAX; i++) {
        if (g_fts_handles[i].handle == f) {
            g_fts_handles[i].handle = NULL;
            g_fts_handles[i].compar = 0;
            return;
        }
    }
}

static int fts_compar_trampoline(const FTSENT **a, const FTSENT **b) {
    VirtualMachine *vm      = cccc_current_ffi_vm();
    long long       args[2] = {(long long)(intptr_t)a, (long long)(intptr_t)b};
    long long       result  = 0;
    if (!vm || cccc_call_guest_callback(vm, g_fts_compar_value, args, 2,
                                        &result) != 0) {
        g_fts_faulted = 1;
        return 0;
    }
    return (int)result;
}

static long long wrap_fts_open(long long path_argv, long long options,
                               long long compar) {
    long long saved_compar  = g_fts_compar_value;
    int       saved_faulted = g_fts_faulted;
    g_fts_compar_value      = compar;
    g_fts_faulted           = 0;

    FTS *f                  = fts_open((char *const *)path_argv, (int)options,
                                       compar ? fts_compar_trampoline : NULL);

    int  faulted            = g_fts_faulted;
    g_fts_compar_value      = saved_compar;
    g_fts_faulted           = saved_faulted;
    if (faulted)
        errno = EFAULT;
    if (f && compar)
        fts_handle_bind(f, compar);
    return (long long)(intptr_t)f;
}

static long long wrap_fts_read(long long ftsp) {
    FTS            *f      = (FTS *)(intptr_t)ftsp;
    long long       compar = fts_handle_compar(f);
    VirtualMachine *vm     = cccc_posix_current_vm();

    if (!compar || !vm || !vm->gil_initialized) {
        // No guest comparator bound: safe to release the GIL for the
        // blocking directory I/O, same as before.
        if (!vm || !vm->gil_initialized)
            return (long long)(intptr_t)fts_read(f);
        ExecState state;
        cccc_posix_save_and_release_gil(vm, &state);
        FTSENT *e = fts_read(f);
        cccc_posix_acquire_and_restore_gil(vm, &state);
        return (long long)(intptr_t)e;
    }

    // A comparator is bound: keep the GIL held (the trampoline reenters
    // vm_eval on this thread) and re-arm the thread-local slot, since
    // wrap_fts_open's own set/restore only covered its own call.
    long long saved_compar  = g_fts_compar_value;
    int       saved_faulted = g_fts_faulted;
    g_fts_compar_value      = compar;
    g_fts_faulted           = 0;

    FTSENT *e               = fts_read(f);

    int     faulted         = g_fts_faulted;
    g_fts_compar_value      = saved_compar;
    g_fts_faulted           = saved_faulted;
    if (faulted)
        errno = EFAULT;
    return (long long)(intptr_t)e;
}

static long long wrap_fts_children(long long ftsp, long long options) {
    FTS            *f      = (FTS *)(intptr_t)ftsp;
    long long       compar = fts_handle_compar(f);
    VirtualMachine *vm     = cccc_posix_current_vm();

    if (!compar || !vm || !vm->gil_initialized) {
        if (!vm || !vm->gil_initialized)
            return (long long)(intptr_t)fts_children(f, (int)options);
        ExecState state;
        cccc_posix_save_and_release_gil(vm, &state);
        FTSENT *e = fts_children(f, (int)options);
        cccc_posix_acquire_and_restore_gil(vm, &state);
        return (long long)(intptr_t)e;
    }

    long long saved_compar  = g_fts_compar_value;
    int       saved_faulted = g_fts_faulted;
    g_fts_compar_value      = compar;
    g_fts_faulted           = 0;

    FTSENT *e               = fts_children(f, (int)options);

    int     faulted         = g_fts_faulted;
    g_fts_compar_value      = saved_compar;
    g_fts_faulted           = saved_faulted;
    if (faulted)
        errno = EFAULT;
    return (long long)(intptr_t)e;
}

static long long wrap_fts_set(long long ftsp, long long f, long long instr) {
    return (long long)fts_set((FTS *)(intptr_t)ftsp, (FTSENT *)(intptr_t)f,
                              (int)instr);
}

static long long wrap_fts_close(long long ftsp) {
    FTS *f = (FTS *)(intptr_t)ftsp;
    fts_handle_unbind(f);
    return (long long)fts_close(f);
}

void register_posix_dir_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "regcomp", (void *)regcomp, 3, 0);
    cc_register_cfunc(vm, "regexec", (void *)regexec, 5, 0);
    cc_register_cfunc(vm, "regerror", (void *)regerror, 4, 0);
    cc_register_cfunc(vm, "regfree", (void *)wrap_regfree, 1, 0);
    cc_register_cfunc(vm, "glob", (void *)wrap_glob, 4, 0);
    cc_register_cfunc(vm, "globfree", (void *)wrap_globfree, 1, 0);
    cc_register_cfunc(vm, "scandir", (void *)wrap_scandir, 4, 0);

    // fts.h (#811)
    cc_register_cfunc(vm, "fts_open", (void *)wrap_fts_open, 3, 0);
    cc_register_cfunc(vm, "fts_read", (void *)wrap_fts_read, 1, 0);
    cc_register_cfunc(vm, "fts_children", (void *)wrap_fts_children, 2, 0);
    cc_register_cfunc(vm, "fts_set", (void *)wrap_fts_set, 3, 0);
    cc_register_cfunc(vm, "fts_close", (void *)wrap_fts_close, 1, 0);

    cc_register_cfunc(vm, "opendir", (void *)opendir, 1, 0);
    cc_register_cfunc(vm, "readdir", (void *)readdir, 1, 0);
    cc_register_cfunc(vm, "readdir_r", (void *)readdir_r, 3, 0);
    cc_register_cfunc(vm, "closedir", (void *)closedir, 1, 0);
    cc_register_cfunc(vm, "alphasort", (void *)alphasort, 2, 0);
    cc_register_cfunc(vm, "seekdir", (void *)seekdir, 2, 0);
    cc_register_cfunc(vm, "telldir", (void *)telldir, 1, 0);
    cc_register_cfunc(vm, "rewinddir", (void *)rewinddir, 1, 0);
}

#else
void register_posix_dir_functions(VirtualMachine *vm) {
    (void)vm;
}
#endif
