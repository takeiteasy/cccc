int thrd_detach(thrd_t thr) {
    pthread_t host;
    __builtin_memcpy(&host, &thr, sizeof(host));
    return pthread_detach(host) == 0 ? 0 : 1;
}
