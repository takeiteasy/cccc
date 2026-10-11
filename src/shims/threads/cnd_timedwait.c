int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts) {
    pthread_cond_t *c = __cccc_ensure_cnd(cond);
    pthread_mutex_t *m = __cccc_ensure_mtx(mtx);
    if (!c || !m || !ts) return 1;
    int rc = pthread_cond_timedwait(c, m, ts);
    if (rc == 0) return 0;
    return rc == ETIMEDOUT ? ETIMEDOUT : 1;
}
