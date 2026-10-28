static short __cccc_native_guest_to_host_pollev(short guest_events) {
#ifdef __APPLE__
    short host = guest_events & (short)~(0x0040 | 0x0080 | 0x0100 | 0x0200);
    if (guest_events & 0x0040) host |= POLLRDNORM;
    if (guest_events & 0x0080) host |= POLLRDBAND;
    if (guest_events & 0x0100) host |= POLLWRNORM;
    if (guest_events & 0x0200) host |= POLLWRBAND;
    return host;
#else
    return guest_events;
#endif
}
static short __cccc_native_host_to_guest_pollev(short host_revents) {
#ifdef __APPLE__
    short guest = host_revents & (short)~(POLLRDNORM | POLLRDBAND | POLLWRBAND);
    if (host_revents & POLLRDNORM) guest |= 0x0040;
    if (host_revents & POLLRDBAND) guest |= 0x0080;
    if (host_revents & POLLWRNORM) guest |= 0x0100;
    if (host_revents & POLLWRBAND) guest |= 0x0200;
    return guest;
#else
    return host_revents;
#endif
}
static struct pollfd *__cccc_native_poll_marshal_in(struct pollfd *guest_fds, nfds_t nfds) {
    struct pollfd *host = (struct pollfd *)malloc(sizeof(struct pollfd) * (nfds ? nfds : 1));
    if (!host) return 0;
    for (nfds_t i = 0; i < nfds; i++) {
        host[i].fd = guest_fds[i].fd;
        host[i].events = __cccc_native_guest_to_host_pollev(guest_fds[i].events);
        host[i].revents = 0;
    }
    return host;
}
static void __cccc_native_poll_marshal_out(struct pollfd *host, struct pollfd *guest_fds, nfds_t nfds) {
    for (nfds_t i = 0; i < nfds; i++)
        guest_fds[i].revents = __cccc_native_host_to_guest_pollev(host[i].revents);
}
