// posix_search.c -- search.h (hsearch/lfind/lsearch/tsearch/tfind/tdelete/
// twalk) plus insque/remque/hcreate/hdestroy (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

// search.h (#809) -- tsearch/tfind/tdelete/lfind/lsearch all take the same
// int (*)(const void *, const void *) comparator shape, so they share one
// trampoline + thread-local slot (each wrapper saves/restores around its
// own call, so there's no cross-call interference). twalk's
// void (*)(const void *, VISIT, int) action gets its own trampoline.
//
// hsearch() takes ENTRY (a 2-pointer struct) by value; CCCC marshals a
// guest struct-by-value FFI argument as a pointer to a caller-side
// scratch copy (the same convention #714 established for vector args,
// confirmed here empirically), so wrap_hsearch's first parameter is a
// pointer to that copy, which it dereferences to build a real host ENTRY
// before making a normal, compiler-generated call to the real hsearch().
static _Thread_local long long g_search_compar_value;
static _Thread_local int g_search_faulted;

static int search_compar_trampoline(const void *a, const void *b) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    long long args[2] = { (long long)(intptr_t)a, (long long)(intptr_t)b };
    long long result = 0;
    if (!vm || cccc_call_guest_callback(vm, g_search_compar_value, args, 2, &result) != 0) {
        g_search_faulted = 1;
        return 0;
    }
    return (int)result;
}

static long long wrap_hsearch(long long entry_ptr, long long action) {
    // ENTRY is passed by value from the guest, which CCCC marshals as a
    // pointer to a caller-side scratch copy of the struct (the same
    // struct-by-value FFI convention #714 established for vector args),
    // not decomposed into separate key/data scalar slots -- confirmed
    // empirically, since a 3-scalar-arg registration silently shifted
    // action into the data slot.
    ENTRY *guest_entry = (ENTRY *)(void *)entry_ptr;
    ENTRY item;
    item.key = guest_entry->key;
    item.data = guest_entry->data;
    ENTRY *r = hsearch(item, (ACTION)action);
    return (long long)(intptr_t)r;
}

static long long wrap_lfind(long long key, long long base, long long nmemb,
                            long long size, long long compar) {
    long long saved = g_search_compar_value;
    int saved_faulted = g_search_faulted;
    g_search_compar_value = compar;
    g_search_faulted = 0;
    void *r = lfind((const void *)key, (const void *)base, (size_t *)nmemb,
                     (size_t)size, compar ? search_compar_trampoline : NULL);
    int faulted = g_search_faulted;
    g_search_compar_value = saved;
    g_search_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return (long long)(intptr_t)r;
}

static long long wrap_lsearch(long long key, long long base, long long nmemb,
                               long long size, long long compar) {
    long long saved = g_search_compar_value;
    int saved_faulted = g_search_faulted;
    g_search_compar_value = compar;
    g_search_faulted = 0;
    void *r = lsearch((const void *)key, (void *)base, (size_t *)nmemb,
                       (size_t)size, compar ? search_compar_trampoline : NULL);
    int faulted = g_search_faulted;
    g_search_compar_value = saved;
    g_search_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return (long long)(intptr_t)r;
}

static long long wrap_tsearch(long long key, long long rootp, long long compar) {
    long long saved = g_search_compar_value;
    int saved_faulted = g_search_faulted;
    g_search_compar_value = compar;
    g_search_faulted = 0;
    void *r = tsearch((const void *)key, (void **)rootp,
                       compar ? search_compar_trampoline : NULL);
    int faulted = g_search_faulted;
    g_search_compar_value = saved;
    g_search_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return (long long)(intptr_t)r;
}

static long long wrap_tfind(long long key, long long rootp, long long compar) {
    long long saved = g_search_compar_value;
    int saved_faulted = g_search_faulted;
    g_search_compar_value = compar;
    g_search_faulted = 0;
    void *r = tfind((const void *)key, (void *const *)rootp,
                     compar ? search_compar_trampoline : NULL);
    int faulted = g_search_faulted;
    g_search_compar_value = saved;
    g_search_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return (long long)(intptr_t)r;
}

static long long wrap_tdelete(long long key, long long rootp, long long compar) {
    long long saved = g_search_compar_value;
    int saved_faulted = g_search_faulted;
    g_search_compar_value = compar;
    g_search_faulted = 0;
    void *r = tdelete((const void *)key, (void **)rootp,
                       compar ? search_compar_trampoline : NULL);
    int faulted = g_search_faulted;
    g_search_compar_value = saved;
    g_search_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return (long long)(intptr_t)r;
}

static _Thread_local long long g_twalk_action_value;
static _Thread_local int g_twalk_faulted;

static void twalk_action_trampoline(const void *nodep, VISIT which, int depth) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    long long args[3] = { (long long)(intptr_t)nodep, (long long)(int)which, (long long)depth };
    long long result = 0;
    if (!vm || cccc_call_guest_callback(vm, g_twalk_action_value, args, 3, &result) != 0)
        g_twalk_faulted = 1;
}

static long long wrap_twalk(long long root, long long action) {
    long long saved = g_twalk_action_value;
    int saved_faulted = g_twalk_faulted;
    g_twalk_action_value = action;
    g_twalk_faulted = 0;
    twalk((const void *)root, action ? twalk_action_trampoline : NULL);
    int faulted = g_twalk_faulted;
    g_twalk_action_value = saved;
    g_twalk_faulted = saved_faulted;
    if (faulted) errno = EFAULT;
    return 0;
}


void register_posix_search_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "hcreate",  (void*)hcreate,  1, 0);
    cc_register_cfunc(vm, "hdestroy", (void*)hdestroy, 0, 0);
    cc_register_cfunc(vm, "hsearch",  (void*)wrap_hsearch, 2, 0);
    cc_register_cfunc(vm, "insque",   (void*)insque,   2, 0);
    cc_register_cfunc(vm, "remque",   (void*)remque,   1, 0);
    cc_register_cfunc(vm, "lfind",    (void*)wrap_lfind,   5, 0);
    cc_register_cfunc(vm, "lsearch",  (void*)wrap_lsearch, 5, 0);
    cc_register_cfunc(vm, "tsearch",  (void*)wrap_tsearch, 3, 0);
    cc_register_cfunc(vm, "tfind",    (void*)wrap_tfind,   3, 0);
    cc_register_cfunc(vm, "tdelete",  (void*)wrap_tdelete, 3, 0);
    cc_register_cfunc(vm, "twalk",    (void*)wrap_twalk,   2, 0);
}

#else
void register_posix_search_functions(VirtualMachine *vm) { (void)vm; }
#endif
