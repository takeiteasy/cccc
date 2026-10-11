int mtx_timedlock(mtx_t *mtx, const struct timespec *ts) {
    pthread_mutex_t *host = __cccc_ensure_mtx(mtx);
    if (!host || !ts) return 1;
    int rc;
#if defined(__linux__)
    rc = pthread_mutex_timedlock(host, ts);
#else
    for (;;) {
        rc = pthread_mutex_trylock(host);
        if (rc == 0) break;
        struct timespec now;
        clock_gettime(0 /* CLOCK_REALTIME */, &now);
        if (now.tv_sec > ts->tv_sec ||
            (now.tv_sec == ts->tv_sec && now.tv_nsec >= ts->tv_nsec)) {
            rc = ETIMEDOUT;
            break;
        }
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
#endif
    if (rc == 0) return 0;
    return rc == ETIMEDOUT ? ETIMEDOUT : 1;
}
