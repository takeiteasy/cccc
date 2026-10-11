#if !defined(__linux__)
static pthread_mutex_t __cccc_nss_native_mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t __cccc_nss_r_layout_size(int count, size_t str_bytes) {
    size_t ptrs = (size_t)(count + 1) * sizeof(char *);
    return ptrs + sizeof(char *) + str_bytes;
}
static char **__cccc_nss_r_copy_ptr_array(char **list, int count, char **cursor, char *end) {
    char *p = (char *)*cursor;
    p = (char *)(((uintptr_t)p + sizeof(char *) - 1) &
                 ~(uintptr_t)(sizeof(char *) - 1));
    char **arr = (char **)(void *)p;
    if ((char *)(arr + count + 1) > end)
        return NULL;
    char *strp = (char *)(arr + count + 1);
    for (int i = 0; i < count; i++) {
        size_t len = __builtin_strlen(list[i]) + 1;
        if (strp + len > end)
            return NULL;
        __builtin_memcpy(strp, list[i], len);
        arr[i] = strp;
        strp += len;
    }
    arr[count] = 0;
    *cursor = strp;
    return arr;
}
static int __cccc_nss_count_list(char **list) {
    int n = 0;
    if (list) while (list[n]) n++;
    return n;
}
#endif
