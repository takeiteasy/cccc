/* signal.h - signal handling declarations for CCCC */

#ifndef __SIGNAL_H
#define __SIGNAL_H

typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

/* Standard POSIX signals (Darwin/macOS values) */
#define SIGHUP   1   /* hangup */
#define SIGINT   2   /* interrupt */
#define SIGQUIT  3   /* quit */
#define SIGILL   4   /* illegal instruction */
#define SIGTRAP  5   /* trace/debugger trap */
#define SIGABRT  6   /* abort */
#define SIGFPE   8   /* floating-point exception */
#define SIGKILL  9   /* kill (cannot be caught or ignored) */
#define SIGBUS  10   /* bus error */
#define SIGSEGV 11   /* segmentation fault */
#define SIGPIPE 13   /* broken pipe */
#define SIGALRM 14   /* alarm clock */
#define SIGTERM 15   /* termination */
#define SIGCHLD 20   /* child status change */
#define SIGUSR1 30   /* user-defined signal 1 */
#define SIGUSR2 31   /* user-defined signal 2 */

/* SIGKILL and SIGSTOP cannot be caught, blocked, or ignored */

extern void (*signal(int sig, void (*func)(int)))(int);
extern int raise(int sig);

#endif /* __SIGNAL_H */
