#if !defined(__linux__)
static int gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen,
                           struct hostent **result, int *h_errnop) {
    pthread_mutex_lock(&__cccc_nss_native_mutex);
    struct hostent *src = gethostbyname(name);
    int rc = 0;
    if (!src) {
        *result = 0;
        if (h_errnop) *h_errnop = HOST_NOT_FOUND;
        goto done;
    }
    {
    int naliases = __cccc_nss_count_list(src->h_aliases);
    int naddrs = __cccc_nss_count_list(src->h_addr_list);
    size_t need = __builtin_strlen(src->h_name) + 1;
    for (int i = 0; i < naliases; i++)
        need += __builtin_strlen(src->h_aliases[i]) + 1;
    need = __cccc_nss_r_layout_size(naliases, need) +
           __cccc_nss_r_layout_size(naddrs, (size_t)naddrs * (size_t)src->h_length);
    if (need > buflen) { rc = ERANGE; *result = 0; goto done; }
    char *cursor = buf;
    char *end = buf + buflen;
    size_t namelen = __builtin_strlen(src->h_name) + 1;
    if (cursor + namelen > end) { rc = ERANGE; *result = 0; goto done; }
    __builtin_memcpy(cursor, src->h_name, namelen);
    ret->h_name = cursor;
    cursor += namelen;
    char **aliases = __cccc_nss_r_copy_ptr_array(src->h_aliases, naliases, &cursor, end);
    if (!aliases) { rc = ERANGE; *result = 0; goto done; }
    ret->h_aliases = aliases;
    ret->h_addrtype = src->h_addrtype;
    ret->h_length = src->h_length;
    char *p = cursor;
    p = (char *)(((uintptr_t)p + sizeof(char *) - 1) &
                 ~(uintptr_t)(sizeof(char *) - 1));
    char **addrs = (char **)(void *)p;
    if ((char *)(addrs + naddrs + 1) > end) { rc = ERANGE; *result = 0; goto done; }
    char *ap = (char *)(addrs + naddrs + 1);
    for (int i = 0; i < naddrs; i++) {
        if (ap + src->h_length > end) { rc = ERANGE; *result = 0; goto done; }
        __builtin_memcpy(ap, src->h_addr_list[i], (size_t)src->h_length);
        addrs[i] = ap;
        ap += src->h_length;
    }
    addrs[naddrs] = 0;
    ret->h_addr_list = addrs;
    *result = ret;
    }
done:
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
    return rc;
}
#endif
