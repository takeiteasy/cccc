#if !defined(__linux__)
static int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param) {
    (void)pid; (void)policy; (void)param;
    errno = ENOSYS;
    return -1;
}
#endif
