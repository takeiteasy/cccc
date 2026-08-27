// -c=native <dlfcn.h> shims (#1105): dlopen/dlsym/dlclose/dlerror
// reproducing the VM's own dynamic-library registry and its refusal to
// dlclose a handle with live dlsym'd symbols.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating and rationale live in the
// matching serialize_*_shims() in src/serialize_shims.c.

// >>> shim: registry
#include <dlfcn.h>
#include <stdlib.h>
struct __cccc_dl_node {
    void *handle;
    int live;
    int closed;
    struct __cccc_dl_node *next;
};
static struct __cccc_dl_node *__cccc_dl_head = 0;
static _Thread_local char *__cccc_dl_error = 0;
static struct __cccc_dl_node *__cccc_dl_find(void *token) {
    for (struct __cccc_dl_node *n = __atomic_load_n(&__cccc_dl_head, __ATOMIC_ACQUIRE); n;
         n = n->next)
        if ((void *)n == token && !__atomic_load_n(&n->closed, __ATOMIC_ACQUIRE))
            return n;
    return 0;
}
static void __cccc_dl_push(struct __cccc_dl_node *n) {
    struct __cccc_dl_node *old_head;
    do {
        old_head = __atomic_load_n(&__cccc_dl_head, __ATOMIC_ACQUIRE);
        n->next = old_head;
    } while (!__atomic_compare_exchange_n(&__cccc_dl_head, &old_head, n, 0,
                                          __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
}
// <<< shim

// >>> shim: dlopen
static void *__cccc_native_dlopen(const char *path, int mode) {
    __cccc_dl_error = 0;
    void *h = dlopen(path, mode ? mode : RTLD_LAZY);
    if (!h) {
        char *err = dlerror();
        __cccc_dl_error = err ? err : "dlopen failed";
        return 0;
    }
    struct __cccc_dl_node *n = malloc(sizeof(*n));
    if (!n) {
        __cccc_dl_error = "dynamic library registry allocation failed";
        return 0;
    }
    n->handle = h;
    n->live = 0;
    n->closed = 0;
    __cccc_dl_push(n);
    return (void *)n;
}
// <<< shim

// >>> shim: dlsym
static void *__cccc_native_dlsym(void *token, const char *symbol) {
    __cccc_dl_error = 0;
    if (!symbol) {
        __cccc_dl_error = "dlsym requires a symbol name";
        return 0;
    }
    struct __cccc_dl_node *n = __cccc_dl_find(token);
    if (!n) {
        __cccc_dl_error = "invalid dynamic library handle";
        return 0;
    }
    dlerror();
    void *ptr = dlsym(n->handle, symbol);
    char *err = dlerror();
    if (err) {
        __cccc_dl_error = err;
        return 0;
    }
    __atomic_fetch_add(&n->live, 1, __ATOMIC_ACQ_REL);
    return ptr;
}
// <<< shim

// >>> shim: dlclose
static int __cccc_native_dlclose(void *token) {
    __cccc_dl_error = 0;
    struct __cccc_dl_node *n = __cccc_dl_find(token);
    if (!n) {
        __cccc_dl_error = "invalid dynamic library handle";
        return -1;
    }
    if (__atomic_load_n(&n->live, __ATOMIC_ACQUIRE) > 0) {
        __cccc_dl_error = "cannot dlclose handle with live callable symbols";
        return -1;
    }
    if (dlclose(n->handle) != 0) {
        char *err = dlerror();
        __cccc_dl_error = err ? err : "dlclose failed";
        return -1;
    }
    __atomic_store_n(&n->closed, 1, __ATOMIC_RELEASE);
    return 0;
}
// <<< shim

// >>> shim: dlerror
static char *__cccc_native_dlerror(void) {
    return __cccc_dl_error;
}
// <<< shim
