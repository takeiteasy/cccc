int cnd_broadcast(cnd_t *cond) {
    pthread_cond_t *c = __cccc_ensure_cnd(cond);
    if (!c) return 1;
    return pthread_cond_broadcast(c) == 0 ? 0 : 1;
}
