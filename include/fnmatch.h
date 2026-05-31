/* fnmatch.h - filename pattern matching for JCC */

#ifndef __FNMATCH_H
#define __FNMATCH_H

#ifdef _WIN32
#error "<fnmatch.h> is only available on POSIX targets in JCC"
#endif

#define FNM_NOMATCH 1

#ifdef __APPLE__
#define FNM_NOESCAPE 0x01
#define FNM_PATHNAME 0x02
#define FNM_PERIOD   0x04
#define FNM_LEADING_DIR 0x08
#define FNM_CASEFOLD 0x10
#else
#define FNM_PATHNAME 0x0001
#define FNM_NOESCAPE 0x0002
#define FNM_PERIOD   0x0004
#define FNM_LEADING_DIR 0x0008
#define FNM_CASEFOLD 0x0010
#endif

#define FNM_FILE_NAME FNM_PATHNAME

extern int fnmatch(const char *pattern, const char *string, int flags);

#endif /* __FNMATCH_H */
