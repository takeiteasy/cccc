/* sys/utsname.h - system identification declarations for CCCC */

#ifndef __SYS_UTSNAME_H
#define __SYS_UTSNAME_H

#ifdef _WIN32
#error "<sys/utsname.h> is only available on POSIX targets in CCCC"
#endif

/* struct utsname (#733/#737). Field count/order is identical on macOS and
   Linux (sysname/nodename/release/version/machine, POSIX-mandated order),
   but the per-field length diverges -- verified via sizeof/offsetof against
   real macOS and Linux x86_64/aarch64 headers: each field is a 256-byte
   char array on macOS (sizeof(struct utsname) == 1280) vs a 65-byte char
   array on Linux (sizeof(struct utsname) == 390, Linux values match across
   x86_64/aarch64). Linux also appends a glibc-only `domainname` field
   (NIS/YP domain name, offset 325) that macOS has no equivalent of at all. */
#ifdef __APPLE__
#define _CCCC_UTSNAME_LEN 256
struct utsname {
    char sysname[_CCCC_UTSNAME_LEN];
    char nodename[_CCCC_UTSNAME_LEN];
    char release[_CCCC_UTSNAME_LEN];
    char version[_CCCC_UTSNAME_LEN];
    char machine[_CCCC_UTSNAME_LEN];
};
#else
#define _CCCC_UTSNAME_LEN 65
struct utsname {
    char sysname[_CCCC_UTSNAME_LEN];
    char nodename[_CCCC_UTSNAME_LEN];
    char release[_CCCC_UTSNAME_LEN];
    char version[_CCCC_UTSNAME_LEN];
    char machine[_CCCC_UTSNAME_LEN];
    char domainname[_CCCC_UTSNAME_LEN];
};
#endif

extern int uname(struct utsname *buf);

#endif /* __SYS_UTSNAME_H */
