/* grp.h - group database declarations for JCC */

#ifndef __GRP_H
#define __GRP_H

#ifdef _WIN32
#error "<grp.h> is only available on POSIX targets in JCC"
#endif

#include "sys/types.h"

struct group {
    char *gr_name;
    char *gr_passwd;
    gid_t gr_gid;
    char **gr_mem;
};

extern struct group *getgrgid(gid_t gid);
extern struct group *getgrnam(const char *name);

#endif /* __GRP_H */
