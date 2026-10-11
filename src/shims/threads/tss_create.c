int tss_create(tss_t *key, tss_dtor_t dtor) {
    return pthread_key_create(key, dtor) == 0 ? 0 : 1;
}
