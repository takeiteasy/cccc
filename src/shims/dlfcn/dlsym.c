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
