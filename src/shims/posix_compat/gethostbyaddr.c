static struct hostent *__cccc_native_gethostbyaddr(const void *addr, socklen_t len, int type) {
#if !defined(__linux__)
    pthread_mutex_lock(&__cccc_nss_native_mutex);
#endif
    struct hostent *r = gethostbyaddr(addr, len, type);
#if !defined(__linux__)
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
#endif
    return r;
}
