#if !defined(__linux__)
static int ppoll(struct pollfd *fds, nfds_t nfds,
                 const struct timespec *timeout,
                 const sigset_t *sigmask) {
    sigset_t old_set;
    int have_old = 0;
    if (sigmask) {
        if (pthread_sigmask(SIG_SETMASK, sigmask, &old_set) == 0)
            have_old = 1;
    }
    int ms = -1;
    if (timeout)
        ms = (int)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000);
    struct pollfd *host = __cccc_native_poll_marshal_in(fds, nfds);
    if (!host) { errno = ENOMEM; return -1; }
    int r = poll(host, nfds, ms);
    int saved_errno = errno;
    __cccc_native_poll_marshal_out(host, fds, nfds);
    free(host);
    if (have_old)
        pthread_sigmask(SIG_SETMASK, &old_set, NULL);
    errno = saved_errno;
    return r;
}
#endif
