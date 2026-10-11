int mtx_trylock(mtx_t *mtx) {
    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);
    if (!host) return 1;
    int rc = pthread_mutex_trylock(host);
    if (rc == 0) return 0;
    return rc == EBUSY ? EBUSY : 1;
}
