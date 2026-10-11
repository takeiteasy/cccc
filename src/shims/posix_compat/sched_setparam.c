#if !defined(__linux__)
static int sched_setparam(pid_t pid, const struct sched_param *param) {
    (void)pid; (void)param;
    errno = ENOSYS;
    return -1;
}
#endif
