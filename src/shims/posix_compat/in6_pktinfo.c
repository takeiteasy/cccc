#if defined(__linux__) && !defined(_GNU_SOURCE) && !defined(__USE_GNU)
struct in6_pktinfo {
    struct in6_addr ipi6_addr;
    int ipi6_ifindex;
};
#endif
