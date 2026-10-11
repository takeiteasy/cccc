int thrd_sleep(const struct timespec *duration, struct timespec *remaining) {
    if (!duration) return -2;
    int rc = nanosleep(duration, remaining);
    if (rc == 0) return 0;
    return errno == EINTR ? -1 : -2;
}
