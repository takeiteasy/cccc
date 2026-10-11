#if !defined(__linux__)
static int sched_getparam(pid_t pid, struct sched_param *param) {
    (void)pid; (void)param;
    errno = ENOSYS;
    return -1;
}
#endif
