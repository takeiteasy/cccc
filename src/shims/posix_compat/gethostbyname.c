static struct hostent *__cccc_native_gethostbyname(const char *name) {
#if !defined(__linux__)
    pthread_mutex_lock(&__cccc_nss_native_mutex);
#endif
    struct hostent *r = gethostbyname(name);
#if !defined(__linux__)
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
#endif
    return r;
}
