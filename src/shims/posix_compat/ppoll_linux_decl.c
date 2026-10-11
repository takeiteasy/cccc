#if defined(__linux__)
extern int ppoll(struct pollfd *fds, nfds_t nfds,
                 const struct timespec *timeout,
                 const sigset_t *sigmask);
#endif
