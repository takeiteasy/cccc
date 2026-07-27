/* sys/select.h - synchronous I/O multiplexing for CCCC
 *
 * sizeof(fd_set) == 128 and FD_SETSIZE == 1024 on both macOS and Linux
 * (verified via probe). The underlying word width used to address bits
 * differs (4 bytes on macOS's `int32_t fds_bits[32]`, 8 bytes on Linux's
 * `long fds_bits[16]`), but both are little-endian on every arch CCCC
 * supports, so bit k always lands at byte k/8, bit k%8 regardless of word
 * width -- fd_set is declared here as a flat byte array and FD_SET/FD_CLR/
 * FD_ISSET operate byte-wise, sidestepping the word-width divergence
 * entirely rather than needing a per-platform layout.
 */

#ifndef __SYS_SELECT_H
#define __SYS_SELECT_H

#ifdef _WIN32
#error "<sys/select.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/time.h"
#include "signal.h"

#define FD_SETSIZE 1024

typedef struct {
    unsigned char __fds_bits[FD_SETSIZE / 8];
} fd_set;

#define FD_ZERO(set) \
    do { \
        for (unsigned __i = 0; __i < sizeof((set)->__fds_bits); __i++) \
            (set)->__fds_bits[__i] = 0; \
    } while (0)

#define FD_SET(fd, set)   ((set)->__fds_bits[(fd) / 8] |=  (unsigned char)(1u << ((fd) % 8)))
#define FD_CLR(fd, set)   ((set)->__fds_bits[(fd) / 8] &= (unsigned char)~(1u << ((fd) % 8)))
#define FD_ISSET(fd, set) (((set)->__fds_bits[(fd) / 8] & (unsigned char)(1u << ((fd) % 8))) != 0)

extern int select(int nfds, fd_set *readfds, fd_set *writefds,
                   fd_set *exceptfds, struct timeval *timeout);
extern int pselect(int nfds, fd_set *readfds, fd_set *writefds,
                    fd_set *exceptfds, const struct timespec *timeout,
                    const sigset_t *sigmask);

#endif /* __SYS_SELECT_H */
