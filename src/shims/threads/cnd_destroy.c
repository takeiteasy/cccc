void cnd_destroy(cnd_t *cond) {
    if (!cond || !cond->__handle) return;
    pthread_cond_destroy((pthread_cond_t *)cond->__handle);
    free(cond->__handle);
    cond->__handle = NULL;
    cond->__state = 0;
}
