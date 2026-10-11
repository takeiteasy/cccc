static pthread_mutex_t *__cccc_ensure_mtx(mtx_t *mtx) {
    if (!mtx) return NULL;
    void *h = __atomic_load_n(&mtx->__handle, __ATOMIC_ACQUIRE);
    if (h) return (pthread_mutex_t *)h;
    pthread_mutex_t *host = malloc(sizeof(*host));
    if (!host) return NULL;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if (mtx->__type == 1)
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(host, &attr) != 0) {
        pthread_mutexattr_destroy(&attr);
        free(host);
        return NULL;
    }
    pthread_mutexattr_destroy(&attr);
    void *expected = NULL;
    if (!__atomic_compare_exchange_n(&mtx->__handle, &expected, host, 0,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        pthread_mutex_destroy(host);
        free(host);
        return (pthread_mutex_t *)expected;
    }
    mtx->__state = 1;
    return host;
}
