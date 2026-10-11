int mtx_init(mtx_t *mtx, int type) {
    if (!mtx) return 1;
    if (__atomic_load_n(&mtx->__handle, __ATOMIC_ACQUIRE))
        return 1;
    mtx->__type = type;
    return __cccc_ensure_mtx(mtx) ? 0 : 1;
}
