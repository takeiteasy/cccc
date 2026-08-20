/* sys/param.h - system parameters and limits for CCCC */

#ifndef __SYS_PARAM_H
#define __SYS_PARAM_H

#ifdef _WIN32
#error "<sys/param.h> is only available on POSIX targets in CCCC"
#endif

#include "limits.h"

/* Maximum length of a pathname, including the terminating NUL */
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#ifndef NBBY
#define NBBY 8 /* number of bits in a byte */
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#endif /* __SYS_PARAM_H */
