int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    struct __cccc_thrd_args *a = malloc(sizeof(*a));
    if (!a) return ENOMEM;
    a->fn = func;
    a->arg = arg;
    pthread_t host;
    int rc = pthread_create(&host, NULL, __cccc_thrd_trampoline, a);
    if (rc != 0) {
        free(a);
        return rc == ENOMEM ? ENOMEM : 1;
    }
    thrd_t out = 0;
    __builtin_memcpy(&out, &host, sizeof(host));
    *thr = out;
    return 0;
}
