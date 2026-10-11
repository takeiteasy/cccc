#if !defined(__linux__)
static int sched_getscheduler(pid_t pid) {
    (void)pid;
    errno = ENOSYS;
    return -1;
}
#endif
