/* netinet/in.h - Internet address declarations for JCC */

#ifndef __NETINET_IN_H
#define __NETINET_IN_H

#ifdef _WIN32
#error "<netinet/in.h> is only available on POSIX targets in JCC"
#endif

#include "stdint.h"
#include "sys/socket.h"

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

#define AF_INET  2
#ifdef __APPLE__
#define AF_INET6 30
#else
#define AF_INET6 10
#endif

#define PF_INET  AF_INET
#define PF_INET6 AF_INET6

#define IPPROTO_IP   0
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

#define INADDR_ANY       ((in_addr_t)0x00000000)
#define INADDR_LOOPBACK  ((in_addr_t)0x7f000001)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_NONE      ((in_addr_t)0xffffffff)

struct in_addr {
    in_addr_t s_addr;
};

#ifdef __APPLE__
struct sockaddr_in {
    unsigned char sin_len;
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};
#else
struct sockaddr_in {
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};
#endif

#endif /* __NETINET_IN_H */
