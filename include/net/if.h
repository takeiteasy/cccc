/* net/if.h - network interface naming for CCCC */

#ifndef __NET_IF_H
#define __NET_IF_H

#ifdef _WIN32
#error "<net/if.h> is only available on POSIX targets in CCCC"
#endif

/* if_nametoindex()/if_indextoname()/if_nameindex()/if_freenameindex() (#788).
   Needed by guest code targeting a specific interface (e.g. for
   IPV6_MULTICAST_IF or struct ipv6_mreq.ipv6mr_interface, see
   <netinet/in.h>) rather than relying on interface index 0. IF_NAMESIZE and
   struct if_nameindex are identical on macOS and Linux -- verified via
   sizeof/offsetof against real macOS and Linux x86_64/aarch64 headers
   (Linux values match across x86_64/aarch64). All four functions exist,
   unguarded, on both platforms. */
#define IF_NAMESIZE 16
#define IFNAMSIZ    IF_NAMESIZE

struct if_nameindex {
    unsigned int  if_index;
    char         *if_name;
};

extern unsigned int if_nametoindex(const char *ifname);
extern char *if_indextoname(unsigned int ifindex, char *ifname);
/* if_nameindex()'s return value, and each if_name string within it, are
   host-allocated -- the guest must release them via if_freenameindex(),
   never free(). */
extern struct if_nameindex *if_nameindex(void);
extern void if_freenameindex(struct if_nameindex *ptr);

#endif /* __NET_IF_H */
