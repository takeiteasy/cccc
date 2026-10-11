int tss_set(tss_t key, void *val) {
    return pthread_setspecific(key, val) == 0 ? 0 : 1;
}
