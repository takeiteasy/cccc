/* netdb.h - network database declarations for JCC */

#ifndef __NETDB_H
#define __NETDB_H

#ifdef _WIN32
#error "<netdb.h> is only available on POSIX targets in JCC"
#endif

#include "sys/socket.h"

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

#define h_addr h_addr_list[0]

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

#define EAI_AGAIN    2
#define EAI_BADFLAGS 3
#define EAI_FAIL     4
#define EAI_FAMILY   5
#define EAI_MEMORY   6
#define EAI_NONAME   8
#define EAI_SERVICE  9
#define EAI_SOCKTYPE 10

extern struct hostent *gethostbyname(const char *name);
extern int getaddrinfo(const char *node, const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res);
extern void freeaddrinfo(struct addrinfo *res);

#endif /* __NETDB_H */
