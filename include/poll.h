/* poll.h - event polling for CCCC
 *
 * POLLRDNORM/POLLRDBAND happen to share the same bit values on macOS and
 * glibc, but POLLWRNORM/POLLWRBAND diverge (macOS aliases POLLWRNORM to
 * POLLOUT and uses 0x0100 for POLLWRBAND; glibc uses 0x0100/0x0200). The
 * constants below use CCCC's own canonical numbering (glibc's values, so
 * Linux needs no translation), the same pattern used for LC_* categories
 * (locale.h) and nl_item (langinfo.h). guest_to_host_pollev/
 * host_to_guest_pollev (src/stdlib/posix.c) translate events in and
 * revents out around poll()/ppoll().
 */

#ifndef __POLL_H
#define __POLL_H

#ifdef _WIN32
#error "<poll.h> is only available on POSIX targets in CCCC"
#endif

#include "signal.h"
#include "time.h"

#define POLLIN   0x0001
#define POLLPRI  0x0002
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

#define POLLRDNORM 0x0040
#define POLLRDBAND 0x0080
#define POLLWRNORM 0x0100
#define POLLWRBAND 0x0200

/* Legacy infinite-timeout constant, -1 on both platforms. */
#define INFTIM (-1)

typedef unsigned int nfds_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};

extern int poll(struct pollfd *fds, nfds_t nfds, int timeout);

/* ppoll() -- the poll()-with-timeout-and-sigmask analog of pselect(). The
 * guest sigset_t -> host sigset_t translation follows the same pattern as
 * wrap_pselect_gil (src/stdlib/posix.c). macOS has no native ppoll(); it is
 * emulated there via pthread_sigmask()+poll(), which is not atomic -- see
 * the wrap_ppoll_gil comment in src/stdlib/posix.c for the race this
 * leaves open.
 */
extern int ppoll(struct pollfd *fds, nfds_t nfds,
                 const struct timespec *timeout, const sigset_t *sigmask);

#endif /* __POLL_H */
