static int __cccc_native_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    struct pollfd *host = __cccc_native_poll_marshal_in(fds, nfds);
    if (!host) { errno = ENOMEM; return -1; }
    int r = poll(host, nfds, timeout);
    int saved_errno = errno;
    __cccc_native_poll_marshal_out(host, fds, nfds);
    free(host);
    errno = saved_errno;
    return r;
}
