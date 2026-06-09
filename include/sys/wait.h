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

extern pid_t wait(int *status);
extern pid_t waitpid(pid_t pid, int *status, int options);

#endif /* __SYS_WAIT_H */
