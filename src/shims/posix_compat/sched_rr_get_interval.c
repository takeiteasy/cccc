#if !defined(__linux__)
static int sched_rr_get_interval(pid_t pid, struct timespec *interval) {
    (void)pid; (void)interval;
    errno = ENOSYS;
    return -1;
}
#endif
