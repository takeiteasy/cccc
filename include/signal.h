/* signal.h - signal handling declarations for CCCC */

#ifndef __SIGNAL_H
#define __SIGNAL_H

typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

/* Standard POSIX signals. Numbers differ between Darwin and Linux past the
   common core (1-15), so the rest are guarded per-platform. */
#define SIGHUP   1   /* hangup */
#define SIGINT   2   /* interrupt */
#define SIGQUIT  3   /* quit */
#define SIGILL   4   /* illegal instruction */
#define SIGTRAP  5   /* trace/debugger trap */
#define SIGABRT  6   /* abort */
#define SIGFPE   8   /* floating-point exception */
#define SIGKILL  9   /* kill (cannot be caught or ignored) */
#define SIGPIPE 13   /* broken pipe */
#define SIGALRM 14   /* alarm clock */
#define SIGTERM 15   /* termination */

#ifdef __APPLE__
#define SIGBUS     10   /* bus error */
#define SIGSEGV    11   /* segmentation fault */
#define SIGSYS     12   /* bad system call */
#define SIGURG     16   /* urgent condition on socket */
#define SIGSTOP    17   /* stop (cannot be caught or ignored) */
#define SIGTSTP    18   /* stop signal from tty */
#define SIGCONT    19   /* continue after stop */
#define SIGCHLD    20   /* child status change */
#define SIGTTIN    21   /* background tty read */
#define SIGTTOU    22   /* background tty write */
#define SIGIO      23   /* I/O now possible */
#define SIGXCPU    24   /* CPU time limit exceeded */
#define SIGXFSZ    25   /* file size limit exceeded */
#define SIGVTALRM  26   /* virtual time alarm */
#define SIGPROF    27   /* profiling time alarm */
#define SIGWINCH   28   /* window size change */
#define SIGUSR1    30   /* user-defined signal 1 */
#define SIGUSR2    31   /* user-defined signal 2 */
#else
#define SIGBUS     7    /* bus error */
#define SIGSEGV    11   /* segmentation fault */
#define SIGUSR1    10   /* user-defined signal 1 */
#define SIGUSR2    12   /* user-defined signal 2 */
#define SIGCHLD    17   /* child status change */
#define SIGCONT    18   /* continue after stop */
#define SIGSTOP    19   /* stop (cannot be caught or ignored) */
#define SIGTSTP    20   /* stop signal from tty */
#define SIGTTIN    21   /* background tty read */
#define SIGTTOU    22   /* background tty write */
#define SIGURG     23   /* urgent condition on socket */
#define SIGXCPU    24   /* CPU time limit exceeded */
#define SIGXFSZ    25   /* file size limit exceeded */
#define SIGVTALRM  26   /* virtual time alarm */
#define SIGPROF    27   /* profiling time alarm */
#define SIGWINCH   28   /* window size change */
#define SIGIO      29   /* I/O now possible */
#define SIGSYS     31   /* bad system call */
#endif

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
