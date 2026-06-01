/* sys/socket.h - socket declarations for JCC */

#ifndef __SYS_SOCKET_H
#define __SYS_SOCKET_H

#ifdef _WIN32
#error "<sys/socket.h> is only available on POSIX targets in JCC"
#endif

#include "sys/types.h"

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

#ifdef __APPLE__
#define SOL_SOCKET   0xffff
#define SO_REUSEADDR 0x0004
#else
#define SOL_SOCKET   1
#define SO_REUSEADDR 2
#endif

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

extern int socket(int domain, int type, int protocol);
extern int bind(int socket, const struct sockaddr *address, socklen_t address_len);
extern int listen(int socket, int backlog);
extern int accept(int socket, struct sockaddr *address, socklen_t *address_len);
extern int connect(int socket, const struct sockaddr *address, socklen_t address_len);
extern int setsockopt(int socket, int level, int option_name,
                      const void *option_value, socklen_t option_len);
extern int getsockname(int socket, struct sockaddr *address, socklen_t *address_len);
extern int shutdown(int socket, int how);

#endif /* __SYS_SOCKET_H */
