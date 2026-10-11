#if !defined(__linux__)
static int getnetbyname_r(const char *name, struct netent *ret, char *buf, size_t buflen,
                          struct netent **result, int *h_errnop) {
    pthread_mutex_lock(&__cccc_nss_native_mutex);
    struct netent *src = getnetbyname(name);
    int rc = 0;
    if (!src) {
        *result = 0;
        if (h_errnop) *h_errnop = HOST_NOT_FOUND;
        goto done;
    }
    {
    int naliases = __cccc_nss_count_list(src->n_aliases);
    size_t need = __builtin_strlen(src->n_name) + 1;
    for (int i = 0; i < naliases; i++)
        need += __builtin_strlen(src->n_aliases[i]) + 1;
    need = __cccc_nss_r_layout_size(naliases, need);
    if (need > buflen) { rc = ERANGE; *result = 0; goto done; }
    char *cursor = buf;
    char *end = buf + buflen;
    size_t namelen = __builtin_strlen(src->n_name) + 1;
    if (cursor + namelen > end) { rc = ERANGE; *result = 0; goto done; }
    __builtin_memcpy(cursor, src->n_name, namelen);
    ret->n_name = cursor;
    cursor += namelen;
    char **aliases = __cccc_nss_r_copy_ptr_array(src->n_aliases, naliases, &cursor, end);
    if (!aliases) { rc = ERANGE; *result = 0; goto done; }
    ret->n_aliases = aliases;
    ret->n_addrtype = src->n_addrtype;
    ret->n_net = src->n_net;
    *result = ret;
    }
done:
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
    return rc;
}
#endif
