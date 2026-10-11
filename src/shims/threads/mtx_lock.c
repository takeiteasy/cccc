int mtx_lock(mtx_t *mtx) {
    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);
    if (!host) return 1;
    return pthread_mutex_lock(host) == 0 ? 0 : 1;
}
