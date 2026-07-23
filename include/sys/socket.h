/* sys/socket.h - socket declarations for CCCC */

#ifndef __SYS_SOCKET_H
#define __SYS_SOCKET_H

#ifdef _WIN32
#error "<sys/socket.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"
#include "unistd.h" /* for ssize_t */

#ifdef __APPLE__
struct sockaddr {
    unsigned char sa_len;
    sa_family_t sa_family;
    char sa_data[14];
};
#else
struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};
#endif

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define AF_UNIX  1
#define AF_LOCAL AF_UNIX
#define PF_UNIX  AF_UNIX
#define PF_LOCAL AF_LOCAL

#ifdef __linux__
/* macOS has no equivalent type-OR'd-into-socket()'s-type-argument flags for
   these; they're a Linux-only extension (SOCK_STREAM|SOCK_CLOEXEC etc). */
#define SOCK_CLOEXEC  0x80000
#define SOCK_NONBLOCK 0x800
#endif

#ifdef __APPLE__
#define SOL_SOCKET   0xffff
#define SO_DEBUG     0x0001
#define SO_ACCEPTCONN 0x0002
#define SO_REUSEADDR 0x0004
#define SO_KEEPALIVE 0x0008
#define SO_DONTROUTE 0x0010
#define SO_BROADCAST 0x0020
#define SO_LINGER    0x0080
#define SO_OOBINLINE 0x0100
#define SO_REUSEPORT 0x0200
#define SO_SNDBUF    0x1001
#define SO_RCVBUF    0x1002
#define SO_SNDTIMEO  0x1005
#define SO_RCVTIMEO  0x1006
#define SO_ERROR     0x1007
#define SO_TYPE      0x1008
/* macOS has no MSG_NOSIGNAL; use SO_NOSIGPIPE (0x1022) via setsockopt instead. */
#define SO_NOSIGPIPE 0x1022
#else
#define SOL_SOCKET   1
#define SO_DEBUG     1
#define SO_REUSEADDR 2
#define SO_TYPE      3
#define SO_ERROR     4
#define SO_DONTROUTE 5
#define SO_BROADCAST 6
#define SO_SNDBUF    7
#define SO_RCVBUF    8
#define SO_KEEPALIVE 9
#define SO_OOBINLINE 10
#define SO_LINGER    13
#define SO_REUSEPORT 15
#define SO_RCVTIMEO  20
#define SO_SNDTIMEO  21
#define SO_ACCEPTCONN 30
#endif

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#ifdef __APPLE__
#define MSG_OOB       0x1
#define MSG_PEEK      0x2
#define MSG_DONTROUTE 0x4
#define MSG_EOR       0x8
#define MSG_TRUNC     0x10
#define MSG_CTRUNC    0x20
#define MSG_WAITALL   0x40
#define MSG_DONTWAIT  0x80
#else
#define MSG_OOB       0x1
#define MSG_PEEK      0x2
#define MSG_DONTROUTE 0x4
#define MSG_CTRUNC    0x8
#define MSG_TRUNC     0x20
#define MSG_DONTWAIT  0x40
#define MSG_EOR       0x80
#define MSG_WAITALL   0x100
#define MSG_NOSIGNAL  0x4000
#endif

extern int socket(int domain, int type, int protocol);
extern int socketpair(int domain, int type, int protocol, int sv[2]);
extern int bind(int socket, const struct sockaddr *address, socklen_t address_len);
extern int listen(int socket, int backlog);
extern int accept(int socket, struct sockaddr *address, socklen_t *address_len);
extern int connect(int socket, const struct sockaddr *address, socklen_t address_len);
extern int setsockopt(int socket, int level, int option_name,
                      const void *option_value, socklen_t option_len);
extern int getsockopt(int socket, int level, int option_name,
                      void *option_value, socklen_t *option_len);
extern int getsockname(int socket, struct sockaddr *address, socklen_t *address_len);
extern int getpeername(int socket, struct sockaddr *address, socklen_t *address_len);
extern int shutdown(int socket, int how);

/* Data transfer */
extern ssize_t recv(int socket, void *buffer, size_t length, int flags);
extern ssize_t send(int socket, const void *buffer, size_t length, int flags);
extern ssize_t recvfrom(int socket, void *buffer, size_t length, int flags,
                        struct sockaddr *address, socklen_t *address_len);
extern ssize_t sendto(int socket, const void *message, size_t length, int flags,
                      const struct sockaddr *dest_addr, socklen_t dest_len);
extern int sockatmark(int fd);

#endif /* __SYS_SOCKET_H */
