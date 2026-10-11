int cnd_wait(cnd_t *cond, mtx_t *mtx) {
    pthread_cond_t *c = __cccc_ensure_cnd(cond);
    pthread_mutex_t *m = __cccc_ensure_mtx(mtx);
    if (!c || !m) return 1;
    return pthread_cond_wait(c, m) == 0 ? 0 : 1;
}
