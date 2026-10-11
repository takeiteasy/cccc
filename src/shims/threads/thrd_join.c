int thrd_join(thrd_t thr, int *res) {
    pthread_t host;
    __builtin_memcpy(&host, &thr, sizeof(host));
    void *retval = NULL;
    if (pthread_join(host, &retval) != 0) return 1;
    if (res) *res = (int)(long)retval;
    return 0;
}
