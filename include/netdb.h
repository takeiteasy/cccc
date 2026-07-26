/* netdb.h - network database declarations for CCCC */

#ifndef __NETDB_H
#define __NETDB_H

#ifdef _WIN32
#error "<netdb.h> is only available on POSIX targets in CCCC"
#endif

#include "stdint.h"
#include "sys/socket.h"

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

#define h_addr h_addr_list[0]

/* struct netent for getnetbyname()/getnetbyaddr() (#743). Field layout is
   identical on macOS and Linux (no sa_family_t-style width divergence
   here) -- verified via sizeof/offsetof against real macOS and Linux
   x86_64/aarch64 headers (Linux values match across x86_64/aarch64;
   sizeof(struct netent) == 24 on both, n_net is a 32-bit network number
   in host byte order). */
struct netent {
    char     *n_name;
    char    **n_aliases;
    int       n_addrtype;
    uint32_t  n_net;
};

/* struct servent / struct protoent (#746). Same design class as struct
   netent above: no sa_family_t-style divergence, layout identical on macOS
   and Linux -- verified via sizeof/offsetof against real macOS and Linux
   x86_64/aarch64 headers (Linux values match across x86_64/aarch64;
   sizeof(struct servent) == 32, sizeof(struct protoent) == 24 on both).
   s_port is in network byte order, matching the real getservbyname(). */
struct servent {
    char  *s_name;
    char **s_aliases;
    int    s_port;
    char  *s_proto;
};

struct protoent {
    char  *p_name;
    char **p_aliases;
    int    p_proto;
};

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
#ifdef __APPLE__
    char *ai_canonname;
    struct sockaddr *ai_addr;
#else
    struct sockaddr *ai_addr;
    char *ai_canonname;
#endif
    struct addrinfo *ai_next;
};

#define AI_PASSIVE     0x00000001
#define AI_CANONNAME   0x00000002
#define AI_NUMERICHOST 0x00000004
#define AI_NUMERICSERV 0x00001000

/* EAI_* differ between macOS (small positive numbers) and Linux glibc
   (negative numbers). Verified against real macOS and Linux x86_64/aarch64
   headers -- Linux values match across x86_64/aarch64. */
#ifdef __APPLE__
#define EAI_AGAIN    2
#define EAI_BADFLAGS 3
#define EAI_FAIL     4
#define EAI_FAMILY   5
#define EAI_MEMORY   6
#define EAI_NONAME   8
#define EAI_SERVICE  9
#define EAI_SOCKTYPE 10
#define EAI_SYSTEM   11
#define EAI_OVERFLOW 14
#else
#define EAI_BADFLAGS  -1
#define EAI_NONAME    -2
#define EAI_AGAIN     -3
#define EAI_FAIL      -4
#define EAI_FAMILY    -6
#define EAI_SOCKTYPE  -7
#define EAI_SERVICE   -8
#define EAI_MEMORY    -10
#define EAI_SYSTEM    -11
#define EAI_OVERFLOW  -12
#endif

/* getnameinfo() flags -- verified against real macOS and Linux
   x86_64/aarch64 headers (Linux values match across x86_64/aarch64). */
#ifdef __APPLE__
#define NI_NOFQDN      0x00000001
#define NI_NUMERICHOST 0x00000002
#define NI_NAMEREQD    0x00000004
#define NI_NUMERICSERV 0x00000008
#define NI_DGRAM       0x00000010
#else
#define NI_NUMERICHOST 0x00000001
#define NI_NUMERICSERV 0x00000002
#define NI_NOFQDN      0x00000004
#define NI_NAMEREQD    0x00000008
#define NI_DGRAM       0x00000010
#endif
#define NI_MAXHOST 1025
#define NI_MAXSERV 32

extern struct hostent *gethostbyname(const char *name);
extern struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type);
extern int getaddrinfo(const char *node, const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res);
extern void freeaddrinfo(struct addrinfo *res);
extern int getnameinfo(const struct sockaddr *addr, socklen_t addrlen,
                       char *host, socklen_t hostlen,
                       char *serv, socklen_t servlen, int flags);
extern struct netent *getnetbyname(const char *name);
extern struct netent *getnetbyaddr(uint32_t net, int type);
extern void setnetent(int stayopen);
extern void endnetent(void);

extern struct servent *getservbyname(const char *name, const char *proto);
extern struct servent *getservbyport(int port, const char *proto);
extern void setservent(int stayopen);
extern void endservent(void);

extern struct protoent *getprotobyname(const char *name);
extern struct protoent *getprotobynumber(int proto);
extern void setprotoent(int stayopen);
extern void endprotoent(void);

#endif /* __NETDB_H */
