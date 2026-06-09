/* poll.h - event polling for CCCC */

#ifndef __POLL_H
#define __POLL_H

#ifdef _WIN32
#error "<poll.h> is only available on POSIX targets in CCCC"
#endif

#define POLLIN   0x0001
#define POLLPRI  0x0002
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

typedef unsigned int nfds_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};

extern int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif /* __POLL_H */
