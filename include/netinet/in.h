/* netinet/in.h - Internet address declarations for CCCC */

#ifndef __NETINET_IN_H
#define __NETINET_IN_H

#ifdef _WIN32
#error "<netinet/in.h> is only available on POSIX targets in CCCC"
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

#define IPPROTO_IP     0
#define IPPROTO_ICMP   1
#define IPPROTO_TCP    6
#define IPPROTO_UDP    17
#define IPPROTO_ICMPV6 58
#define IPPROTO_RAW    255

#define IPPORT_RESERVED 1024

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

/* IPv6 (#742). struct in6_addr is a flat 16-byte address on both
   platforms. struct sockaddr_in6 follows the same sin_len/1-byte
   sa_family_t (Apple) vs no-sin6_len/2-byte sa_family_t (Linux) trick
   already used for sockaddr/sockaddr_in/sockaddr_un -- verified via
   offsetof/sizeof against real macOS and Linux x86_64/aarch64 headers
   (Linux values match across x86_64/aarch64; sizeof(struct sockaddr_in6)
   == 28 on both). */
struct in6_addr {
    unsigned char s6_addr[16];
};

#ifdef __APPLE__
struct sockaddr_in6 {
    unsigned char   sin6_len;
    sa_family_t     sin6_family;
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};
#else
struct sockaddr_in6 {
    sa_family_t     sin6_family;
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};
#endif

#define IN6ADDR_ANY_INIT      { { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 } }
#define IN6ADDR_LOOPBACK_INIT { { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 } }

#define IPPROTO_IPV6 41

/* IPV6_* socket-option constants -- verified against real macOS and Linux
   x86_64/aarch64 headers (Linux values match across x86_64/aarch64). */
#ifdef __APPLE__
#define IPV6_UNICAST_HOPS   4
#define IPV6_MULTICAST_IF   9
#define IPV6_MULTICAST_HOPS 10
#define IPV6_MULTICAST_LOOP 11
#define IPV6_JOIN_GROUP     12
#define IPV6_LEAVE_GROUP    13
#define IPV6_V6ONLY         27
#else
#define IPV6_UNICAST_HOPS   16
#define IPV6_MULTICAST_IF   17
#define IPV6_MULTICAST_HOPS 18
#define IPV6_MULTICAST_LOOP 19
#define IPV6_JOIN_GROUP     20
#define IPV6_LEAVE_GROUP    21
#define IPV6_V6ONLY         26
#endif

#endif /* __NETINET_IN_H */
