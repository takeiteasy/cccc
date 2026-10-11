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
