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

typedef unsigned int sigset_t;

struct sigaction {
    void     (*sa_handler)(int);
    sigset_t   sa_mask;
    int        sa_flags;
};

extern int sigaction(int sig, const struct sigaction *restrict act,
                     struct sigaction *restrict oact);
extern int sigemptyset(sigset_t *set);
extern int sigfillset(sigset_t *set);
extern int sigaddset(sigset_t *set, int signo);
extern int sigdelset(sigset_t *set, int signo);
extern int sigismember(const sigset_t *set, int signo);

#define SA_RESTART  0x0002
#define SA_RESETHAND 0x0004
#define SA_NOCLDSTOP 0x0008
#define SA_NOCLDWAIT 0x0020
#define SA_NODEFER   0x0010

extern void (*signal(int sig, void (*func)(int)))(int);
extern int raise(int sig);

#endif /* __SIGNAL_H */
