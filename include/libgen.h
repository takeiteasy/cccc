/* libgen.h - pathname manipulation for CCCC */

#ifndef __LIBGEN_H
#define __LIBGEN_H

#ifdef _WIN32
#error "<libgen.h> is only available on POSIX targets in CCCC"
#endif

extern char *basename(char *path);
extern char *dirname(char *path);

#endif /* __LIBGEN_H */
