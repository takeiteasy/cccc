/* sys/un.h - Unix domain socket address declarations for CCCC */

#ifndef __SYS_UN_H
#define __SYS_UN_H

#ifdef _WIN32
#error "<sys/un.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"
#include "sys/socket.h"

#ifdef __APPLE__
struct sockaddr_un {
    unsigned char sun_len;
    sa_family_t   sun_family;
    char          sun_path[104];
};
#else
struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[108];
};
#endif

#endif /* __SYS_UN_H */
