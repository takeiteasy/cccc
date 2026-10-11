static struct netent *__cccc_native_getnetbyname(const char *name) {
#if !defined(__linux__)
    pthread_mutex_lock(&__cccc_nss_native_mutex);
#endif
    struct netent *r = getnetbyname(name);
#if !defined(__linux__)
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
#endif
    return r;
}
