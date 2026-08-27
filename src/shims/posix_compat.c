// -c=native --posix-emulation compat shims (#1140/#1145/#1146): ppoll,
// the sched_* process-scheduling family, the gethostbyname_r resolver
// family + their plain-lookup mutex peers, aio_fsync, struct in6_pktinfo.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating and rationale live in the
// matching serialize_*_shims() in src/serialize_shims.c.

// >>> shim: in6_pktinfo
#if defined(__linux__) && !defined(_GNU_SOURCE) && !defined(__USE_GNU)
struct in6_pktinfo {
    struct in6_addr ipi6_addr;
    int ipi6_ifindex;
};
#endif
// <<< shim

// >>> shim: aio_fsync
static int __cccc_native_aio_fsync(int op, struct aiocb *aiocbp) {
    if (!aiocbp) { errno = EINVAL; return -1; }
    return aio_fsync(op, aiocbp);
}
// <<< shim

// >>> shim: includes
#if !defined(__linux__)
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#endif
// <<< shim

// >>> shim: poll_marshal
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
    short guest = host_revents & (short)~(POLLRDNORM | POLLRDBAND | POLLWRNORM | POLLWRBAND);
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
// <<< shim

// >>> shim: poll
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
// <<< shim

// >>> shim: ppoll
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
// <<< shim

// >>> shim: ppoll_linux_decl
#if defined(__linux__)
extern int ppoll(struct pollfd *fds, nfds_t nfds,
                 const struct timespec *timeout,
                 const sigset_t *sigmask);
#endif
// <<< shim

// >>> shim: sched_setparam
#if !defined(__linux__)
static int sched_setparam(pid_t pid, const struct sched_param *param) {
    (void)pid; (void)param;
    errno = ENOSYS;
    return -1;
}
#endif
// <<< shim

// >>> shim: sched_getparam
#if !defined(__linux__)
static int sched_getparam(pid_t pid, struct sched_param *param) {
    (void)pid; (void)param;
    errno = ENOSYS;
    return -1;
}
#endif
// <<< shim

// >>> shim: sched_setscheduler
#if !defined(__linux__)
static int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param) {
    (void)pid; (void)policy; (void)param;
    errno = ENOSYS;
    return -1;
}
#endif
// <<< shim

// >>> shim: sched_getscheduler
#if !defined(__linux__)
static int sched_getscheduler(pid_t pid) {
    (void)pid;
    errno = ENOSYS;
    return -1;
}
#endif
// <<< shim

// >>> shim: sched_rr_get_interval
#if !defined(__linux__)
static int sched_rr_get_interval(pid_t pid, struct timespec *interval) {
    (void)pid; (void)interval;
    errno = ENOSYS;
    return -1;
}
#endif
// <<< shim

// >>> shim: nss_helpers
#if !defined(__linux__)
static pthread_mutex_t __cccc_nss_native_mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t __cccc_nss_r_layout_size(int count, size_t str_bytes) {
    size_t ptrs = (size_t)(count + 1) * sizeof(char *);
    return ptrs + sizeof(char *) + str_bytes;
}
static char **__cccc_nss_r_copy_ptr_array(char **list, int count, char **cursor, char *end) {
    char *p = (char *)*cursor;
    p = (char *)(((uintptr_t)p + sizeof(char *) - 1) &
                 ~(uintptr_t)(sizeof(char *) - 1));
    char **arr = (char **)(void *)p;
    if ((char *)(arr + count + 1) > end)
        return NULL;
    char *strp = (char *)(arr + count + 1);
    for (int i = 0; i < count; i++) {
        size_t len = __builtin_strlen(list[i]) + 1;
        if (strp + len > end)
            return NULL;
        __builtin_memcpy(strp, list[i], len);
        arr[i] = strp;
        strp += len;
    }
    arr[count] = 0;
    *cursor = strp;
    return arr;
}
static int __cccc_nss_count_list(char **list) {
    int n = 0;
    if (list) while (list[n]) n++;
    return n;
}
#endif
// <<< shim

// >>> shim: gethostbyname_r
#if !defined(__linux__)
static int gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen,
                           struct hostent **result, int *h_errnop) {
    pthread_mutex_lock(&__cccc_nss_native_mutex);
    struct hostent *src = gethostbyname(name);
    int rc = 0;
    if (!src) {
        *result = 0;
        if (h_errnop) *h_errnop = HOST_NOT_FOUND;
        goto done;
    }
    {
    int naliases = __cccc_nss_count_list(src->h_aliases);
    int naddrs = __cccc_nss_count_list(src->h_addr_list);
    size_t need = __builtin_strlen(src->h_name) + 1;
    for (int i = 0; i < naliases; i++)
        need += __builtin_strlen(src->h_aliases[i]) + 1;
    need = __cccc_nss_r_layout_size(naliases, need) +
           __cccc_nss_r_layout_size(naddrs, (size_t)naddrs * (size_t)src->h_length);
    if (need > buflen) { rc = ERANGE; *result = 0; goto done; }
    char *cursor = buf;
    char *end = buf + buflen;
    size_t namelen = __builtin_strlen(src->h_name) + 1;
    if (cursor + namelen > end) { rc = ERANGE; *result = 0; goto done; }
    __builtin_memcpy(cursor, src->h_name, namelen);
    ret->h_name = cursor;
    cursor += namelen;
    char **aliases = __cccc_nss_r_copy_ptr_array(src->h_aliases, naliases, &cursor, end);
    if (!aliases) { rc = ERANGE; *result = 0; goto done; }
    ret->h_aliases = aliases;
    ret->h_addrtype = src->h_addrtype;
    ret->h_length = src->h_length;
    char *p = cursor;
    p = (char *)(((uintptr_t)p + sizeof(char *) - 1) &
                 ~(uintptr_t)(sizeof(char *) - 1));
    char **addrs = (char **)(void *)p;
    if ((char *)(addrs + naddrs + 1) > end) { rc = ERANGE; *result = 0; goto done; }
    char *ap = (char *)(addrs + naddrs + 1);
    for (int i = 0; i < naddrs; i++) {
        if (ap + src->h_length > end) { rc = ERANGE; *result = 0; goto done; }
        __builtin_memcpy(ap, src->h_addr_list[i], (size_t)src->h_length);
        addrs[i] = ap;
        ap += src->h_length;
    }
    addrs[naddrs] = 0;
    ret->h_addr_list = addrs;
    *result = ret;
    }
done:
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
    return rc;
}
#endif
// <<< shim

// >>> shim: gethostbyaddr_r
#if !defined(__linux__)
static int gethostbyaddr_r(const void *addr, socklen_t len, int type, struct hostent *ret, char *buf,
                           size_t buflen, struct hostent **result, int *h_errnop) {
    pthread_mutex_lock(&__cccc_nss_native_mutex);
    struct hostent *src = gethostbyaddr(addr, len, type);
    int rc = 0;
    if (!src) {
        *result = 0;
        if (h_errnop) *h_errnop = HOST_NOT_FOUND;
        goto done;
    }
    {
    int naliases = __cccc_nss_count_list(src->h_aliases);
    int naddrs = __cccc_nss_count_list(src->h_addr_list);
    size_t need = __builtin_strlen(src->h_name) + 1;
    for (int i = 0; i < naliases; i++)
        need += __builtin_strlen(src->h_aliases[i]) + 1;
    need = __cccc_nss_r_layout_size(naliases, need) +
           __cccc_nss_r_layout_size(naddrs, (size_t)naddrs * (size_t)src->h_length);
    if (need > buflen) { rc = ERANGE; *result = 0; goto done; }
    char *cursor = buf;
    char *end = buf + buflen;
    size_t namelen = __builtin_strlen(src->h_name) + 1;
    if (cursor + namelen > end) { rc = ERANGE; *result = 0; goto done; }
    __builtin_memcpy(cursor, src->h_name, namelen);
    ret->h_name = cursor;
    cursor += namelen;
    char **aliases = __cccc_nss_r_copy_ptr_array(src->h_aliases, naliases, &cursor, end);
    if (!aliases) { rc = ERANGE; *result = 0; goto done; }
    ret->h_aliases = aliases;
    ret->h_addrtype = src->h_addrtype;
    ret->h_length = src->h_length;
    char *p = cursor;
    p = (char *)(((uintptr_t)p + sizeof(char *) - 1) &
                 ~(uintptr_t)(sizeof(char *) - 1));
    char **addrs = (char **)(void *)p;
    if ((char *)(addrs + naddrs + 1) > end) { rc = ERANGE; *result = 0; goto done; }
    char *ap = (char *)(addrs + naddrs + 1);
    for (int i = 0; i < naddrs; i++) {
        if (ap + src->h_length > end) { rc = ERANGE; *result = 0; goto done; }
        __builtin_memcpy(ap, src->h_addr_list[i], (size_t)src->h_length);
        addrs[i] = ap;
        ap += src->h_length;
    }
    addrs[naddrs] = 0;
    ret->h_addr_list = addrs;
    *result = ret;
    }
done:
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
    return rc;
}
#endif
// <<< shim

// >>> shim: getnetbyname_r
#if !defined(__linux__)
static int getnetbyname_r(const char *name, struct netent *ret, char *buf, size_t buflen,
                          struct netent **result, int *h_errnop) {
    pthread_mutex_lock(&__cccc_nss_native_mutex);
    struct netent *src = getnetbyname(name);
    int rc = 0;
    if (!src) {
        *result = 0;
        if (h_errnop) *h_errnop = HOST_NOT_FOUND;
        goto done;
    }
    {
    int naliases = __cccc_nss_count_list(src->n_aliases);
    size_t need = __builtin_strlen(src->n_name) + 1;
    for (int i = 0; i < naliases; i++)
        need += __builtin_strlen(src->n_aliases[i]) + 1;
    need = __cccc_nss_r_layout_size(naliases, need);
    if (need > buflen) { rc = ERANGE; *result = 0; goto done; }
    char *cursor = buf;
    char *end = buf + buflen;
    size_t namelen = __builtin_strlen(src->n_name) + 1;
    if (cursor + namelen > end) { rc = ERANGE; *result = 0; goto done; }
    __builtin_memcpy(cursor, src->n_name, namelen);
    ret->n_name = cursor;
    cursor += namelen;
    char **aliases = __cccc_nss_r_copy_ptr_array(src->n_aliases, naliases, &cursor, end);
    if (!aliases) { rc = ERANGE; *result = 0; goto done; }
    ret->n_aliases = aliases;
    ret->n_addrtype = src->n_addrtype;
    ret->n_net = src->n_net;
    *result = ret;
    }
done:
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
    return rc;
}
#endif
// <<< shim

// >>> shim: gethostbyname
static struct hostent *__cccc_native_gethostbyname(const char *name) {
#if !defined(__linux__)
    pthread_mutex_lock(&__cccc_nss_native_mutex);
#endif
    struct hostent *r = gethostbyname(name);
#if !defined(__linux__)
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
#endif
    return r;
}
// <<< shim

// >>> shim: gethostbyaddr
static struct hostent *__cccc_native_gethostbyaddr(const void *addr, socklen_t len, int type) {
#if !defined(__linux__)
    pthread_mutex_lock(&__cccc_nss_native_mutex);
#endif
    struct hostent *r = gethostbyaddr(addr, len, type);
#if !defined(__linux__)
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
#endif
    return r;
}
// <<< shim

// >>> shim: getnetbyname
static struct netent *__cccc_native_getnetbyname(const char *name) {
#if !defined(__linux__)
    pthread_mutex_lock(&__cccc_nss_native_mutex);
#endif
    struct netent *r = getnetbyname(name);
#if !defined(__linux__)
    pthread_mutex_unlock(&__cccc_nss_native_mutex);
#endif
    return r;
}
// <<< shim
