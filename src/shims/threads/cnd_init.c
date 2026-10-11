int cnd_init(cnd_t *cond) {
    if (!cond) return 1;
    return __cccc_ensure_cnd(cond) ? 0 : 1;
}
