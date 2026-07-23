/* sys/wait.h - process wait declarations for CCCC */

#ifndef __SYS_WAIT_H
#define __SYS_WAIT_H

#ifdef _WIN32
#error "<sys/wait.h> is only available on POSIX targets in CCCC"
#endif

#include "unistd.h"

#define WNOHANG    1
#define WUNTRACED  2

#define WIFEXITED(status)   (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) ((((status) & 0x7f) > 0) && (((status) & 0x7f) < 0x7f))
#define WTERMSIG(status)    ((status) & 0x7f)
#define WIFSTOPPED(status)  (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status)    WEXITSTATUS(status)
#define WCOREDUMP(status)   ((status) & 0x80)

#ifdef __APPLE__
#define WCONTINUED 0x10
#define WIFCONTINUED(status) (WSTOPSIG(status) == 0x13)
/* waitid() 4th-argument flags (verified against real macOS header). */
#define WEXITED  0x00000004
#define WSTOPPED 0x00000008
#define WNOWAIT  0x00000020
#else
#define WCONTINUED 8
#define WIFCONTINUED(status) ((status) == 0xffff)
/* waitid() 4th-argument flags (verified against real Linux x86_64/aarch64
   headers -- values match across x86_64/aarch64). */
#define WSTOPPED 2
#define WEXITED  4
#define WNOWAIT  0x01000000
#endif

extern pid_t wait(int *status);
extern pid_t waitpid(pid_t pid, int *status, int options);

/* waitid() is deferred: it needs a guest-visible siginfo_t, which does not
   yet exist in include/signal.h -- the same struct-layout work class as the
   items tracked in the follow-up ticket for this POSIX coverage pass. */

#endif /* __SYS_WAIT_H */
