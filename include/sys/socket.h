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
    sa_family_t   sa_family;
    char          sa_data[14];
};
#else
struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};
#endif

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define AF_UNIX     1
#define AF_LOCAL    AF_UNIX
#define PF_UNIX     AF_UNIX
#define PF_LOCAL    AF_LOCAL

#ifdef __linux__
/* macOS has no equivalent type-OR'd-into-socket()'s-type-argument flags for
   these; they're a Linux-only extension (SOCK_STREAM|SOCK_CLOEXEC etc). */
#define SOCK_CLOEXEC  0x80000
#define SOCK_NONBLOCK 0x800
#endif

#ifdef __APPLE__
#define SOL_SOCKET    0xffff
#define SO_DEBUG      0x0001
#define SO_ACCEPTCONN 0x0002
#define SO_REUSEADDR  0x0004
#define SO_KEEPALIVE  0x0008
#define SO_DONTROUTE  0x0010
#define SO_BROADCAST  0x0020
#define SO_LINGER     0x0080
#define SO_OOBINLINE  0x0100
#define SO_REUSEPORT  0x0200
#define SO_SNDBUF     0x1001
#define SO_RCVBUF     0x1002
#define SO_SNDTIMEO   0x1005
#define SO_RCVTIMEO   0x1006
#define SO_ERROR      0x1007
#define SO_TYPE       0x1008
/* SO_NOSIGPIPE (setsockopt-level, suppresses SIGPIPE for the socket's
   lifetime) predates MSG_NOSIGNAL on macOS and is the idiomatic way to get
   this behavior, but modern macOS SDKs (verified: MacOSX14/14.5/15/15.5)
   also define MSG_NOSIGNAL as a real per-call send() flag, so both are
   provided rather than only the historical one. */
#define SO_NOSIGPIPE 0x1022
#else
#define SOL_SOCKET    1
#define SO_DEBUG      1
#define SO_REUSEADDR  2
#define SO_TYPE       3
#define SO_ERROR      4
#define SO_DONTROUTE  5
#define SO_BROADCAST  6
#define SO_SNDBUF     7
#define SO_RCVBUF     8
#define SO_KEEPALIVE  9
#define SO_OOBINLINE  10
#define SO_LINGER     13
#define SO_REUSEPORT  15
#define SO_RCVTIMEO   20
#define SO_SNDTIMEO   21
#define SO_ACCEPTCONN 30
#endif

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

/* struct msghdr / struct cmsghdr for sendmsg()/recvmsg() ancillary
   (control) data (#741). msg_iovlen/msg_controllen and cmsg_len are
   socklen_t/int (4 bytes) on macOS vs size_t (8 bytes) on Linux --
   verified via offsetof/sizeof against real macOS and Linux
   x86_64/aarch64 headers (Linux values match across x86_64/aarch64).
   struct iovec comes from sys/uio.h, transitively included via
   unistd.h above (#792). */
#ifdef __APPLE__
struct msghdr {
    void         *msg_name;
    socklen_t     msg_namelen;
    struct iovec *msg_iov;
    int           msg_iovlen;
    void         *msg_control;
    socklen_t     msg_controllen;
    int           msg_flags;
};

struct cmsghdr {
    socklen_t cmsg_len;
    int       cmsg_level;
    int       cmsg_type;
};
#else
struct msghdr {
    void         *msg_name;
    socklen_t     msg_namelen;
    struct iovec *msg_iov;
    size_t        msg_iovlen;
    void         *msg_control;
    size_t        msg_controllen;
    int           msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int    cmsg_level;
    int    cmsg_type;
};
#endif

#define SCM_RIGHTS 1

/* CMSG_ALIGN rounds to the width of cmsg_len -- 4 bytes (sizeof(int)) on
   macOS (__DARWIN_ALIGN32), 8 bytes (sizeof(size_t)) on 64-bit Linux.
   This is our own definition, not the host's private macro -- verified
   sizeof(struct cmsghdr) == 12 (macOS) / 16 (Linux) so CMSG_SPACE/CMSG_LEN
   land on the same byte offsets as the host ABI. */
#ifdef __APPLE__
#define __CCCC_CMSG_ALIGN(n)                                                   \
    (((size_t)(n) + sizeof(int) - 1) & ~(sizeof(int) - 1))
#else
#define __CCCC_CMSG_ALIGN(n)                                                   \
    (((size_t)(n) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#endif

#define CMSG_ALIGN(n) __CCCC_CMSG_ALIGN(n)
#define CMSG_SPACE(len)                                                        \
    (__CCCC_CMSG_ALIGN(sizeof(struct cmsghdr)) + __CCCC_CMSG_ALIGN(len))
#define CMSG_LEN(len) (__CCCC_CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_FIRSTHDR(mhdr)                                                    \
    ((size_t)(mhdr)->msg_controllen >= sizeof(struct cmsghdr)                  \
         ? (struct cmsghdr *)(mhdr)->msg_control                               \
         : (struct cmsghdr *)0)
#define CMSG_DATA(cmsg)                                                        \
    ((unsigned char *)(cmsg) + __CCCC_CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_NXTHDR(mhdr, cmsg)                                                \
    (((unsigned char *)(cmsg) + __CCCC_CMSG_ALIGN((cmsg)->cmsg_len) +          \
          sizeof(struct cmsghdr) >                                             \
      (unsigned char *)(mhdr)->msg_control + (mhdr)->msg_controllen)           \
         ? (struct cmsghdr *)0                                                 \
         : (struct cmsghdr *)((unsigned char *)(cmsg) +                        \
                              __CCCC_CMSG_ALIGN((cmsg)->cmsg_len)))

#ifdef __APPLE__
#define MSG_OOB       0x1
#define MSG_PEEK      0x2
#define MSG_DONTROUTE 0x4
#define MSG_EOR       0x8
#define MSG_TRUNC     0x10
#define MSG_CTRUNC    0x20
#define MSG_WAITALL   0x40
#define MSG_DONTWAIT  0x80
#define MSG_NOSIGNAL  0x80000
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
extern int bind(int socket, const struct sockaddr *address,
                socklen_t address_len);
extern int listen(int socket, int backlog);
extern int accept(int socket, struct sockaddr *address, socklen_t *address_len);
extern int connect(int socket, const struct sockaddr *address,
                   socklen_t address_len);
extern int setsockopt(int socket, int level, int option_name,
                      const void *option_value, socklen_t option_len);
extern int getsockopt(int socket, int level, int option_name,
                      void *option_value, socklen_t *option_len);
extern int getsockname(int socket, struct sockaddr *address,
                       socklen_t *address_len);
extern int getpeername(int socket, struct sockaddr *address,
                       socklen_t *address_len);
extern int shutdown(int socket, int how);

/* Data transfer */
extern ssize_t recv(int socket, void *buffer, size_t length, int flags);
extern ssize_t send(int socket, const void *buffer, size_t length, int flags);
extern ssize_t recvfrom(int socket, void *buffer, size_t length, int flags,
                        struct sockaddr *address, socklen_t *address_len);
extern ssize_t sendto(int socket, const void *message, size_t length, int flags,
                      const struct sockaddr *dest_addr, socklen_t dest_len);
extern ssize_t sendmsg(int socket, const struct msghdr *message, int flags);
extern ssize_t recvmsg(int socket, struct msghdr *message, int flags);
extern int sockatmark(int fd);

#endif /* __SYS_SOCKET_H */
