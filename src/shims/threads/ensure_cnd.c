static pthread_cond_t *__cccc_ensure_cnd(cnd_t *cond) {
    if (!cond) return NULL;
    void *h = __atomic_load_n(&cond->__handle, __ATOMIC_ACQUIRE);
    if (h) return (pthread_cond_t *)h;
    pthread_cond_t *host = malloc(sizeof(*host));
    if (!host) return NULL;
    if (pthread_cond_init(host, NULL) != 0) {
        free(host);
        return NULL;
    }
    void *expected = NULL;
    if (!__atomic_compare_exchange_n(&cond->__handle, &expected, host, 0,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        pthread_cond_destroy(host);
        free(host);
        return (pthread_cond_t *)expected;
    }
    cond->__state = 1;
    return host;
}
