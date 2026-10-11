void mtx_destroy(mtx_t *mtx) {
    if (!mtx || !mtx->__handle) return;
    pthread_mutex_destroy((pthread_mutex_t *)mtx->__handle);
    free(mtx->__handle);
    mtx->__handle = NULL;
    mtx->__state = 0;
}
