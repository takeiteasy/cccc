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
