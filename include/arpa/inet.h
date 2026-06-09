/* arpa/inet.h - Internet address manipulation for CCCC */

#ifndef __ARPA_INET_H
#define __ARPA_INET_H

#ifdef _WIN32
#error "<arpa/inet.h> is only available on POSIX targets in CCCC"
#endif

#include "stddef.h"
#include "netinet/in.h"

extern uint32_t htonl(uint32_t hostlong);
extern uint16_t htons(uint16_t hostshort);
extern uint32_t ntohl(uint32_t netlong);
extern uint16_t ntohs(uint16_t netshort);

extern uint32_t inet_addr(const char *cp);
extern char *inet_ntoa(struct in_addr in);
extern const char *inet_ntop(int af, const void *src, char *dst, size_t size);
extern int inet_pton(int af, const char *src, void *dst);

#endif /* __ARPA_INET_H */
