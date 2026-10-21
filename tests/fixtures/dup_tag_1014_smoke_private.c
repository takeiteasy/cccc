struct DyGC1014Smoke {
    double d;
    char   pad;
};
int priv_use_1014_smoke(void) {
    struct DyGC1014Smoke x;
    x.d   = 1.0;
    x.pad = 'a';
    return (int)x.d;
}
