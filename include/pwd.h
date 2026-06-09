/* pwd.h - password database declarations for CCCC */

#ifndef __PWD_H
#define __PWD_H

#ifdef _WIN32
#error "<pwd.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"

struct passwd {
    char *pw_name;
    char *pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
#ifdef __APPLE__
    long pw_change;
    char *pw_class;
#endif
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
#ifdef __APPLE__
    long pw_expire;
#endif
};

extern struct passwd *getpwuid(uid_t uid);
extern struct passwd *getpwnam(const char *name);

#endif /* __PWD_H */
