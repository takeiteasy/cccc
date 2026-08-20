/* grp.h - group database declarations for CCCC */

#ifndef __GRP_H
#define __GRP_H

#ifdef _WIN32
#error "<grp.h> is only available on POSIX targets in CCCC"
#endif

#include "stddef.h"
#include "sys/types.h"

struct group {
    char  *gr_name;
    char  *gr_passwd;
    gid_t  gr_gid;
    char **gr_mem;
};

extern struct group *getgrgid(gid_t gid);
extern struct group *getgrnam(const char *name);
extern int getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t buflen,
                      struct group **result);
extern int getgrnam_r(const char *name, struct group *grp, char *buf,
                      size_t buflen, struct group **result);

#endif /* __GRP_H */
