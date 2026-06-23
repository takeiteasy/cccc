/* sys/file.h - advisory file locking for CCCC */

#ifndef __SYS_FILE_H
#define __SYS_FILE_H

#ifdef _WIN32
#error "<sys/file.h> is only available on POSIX targets in CCCC"
#endif

#include "fcntl.h"

/* flock() operations */
#define LOCK_SH 1   /* shared lock */
#define LOCK_EX 2   /* exclusive lock */
#define LOCK_NB 4   /* don't block when locking */
#define LOCK_UN 8   /* unlock */

extern int flock(int fd, int operation);

#endif /* __SYS_FILE_H */
