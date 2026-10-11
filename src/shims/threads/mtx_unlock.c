int mtx_unlock(mtx_t *mtx) {
    if (!mtx || !mtx->__handle) return 1;
    return pthread_mutex_unlock((pthread_mutex_t *)mtx->__handle) == 0 ? 0 : 1;
}
