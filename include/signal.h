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

/* siginfo_t for waitid() (#744). Host waitid() writes a full host-sized
   siginfo_t through this pointer, so the guest struct must be at least as
   large as the real one or the host call overflows into adjacent guest
   memory -- sized to the full host sizeof (104 bytes macOS, 128 bytes
   Linux) with explicit trailing padding, not a smaller "just the fields we
   use" struct. si_signo/si_errno/si_code/si_pid/si_uid/si_status are laid
   out at their real offsetof positions (verified against real macOS and
   Linux x86_64/aarch64 headers -- Linux values match across
   x86_64/aarch64; note Linux packs si_pid/si_uid at offset 16/20, behind 4
   bytes of padding after si_code, where macOS packs them contiguously at
   12/16 with no padding). si_addr and the rest of each platform's
   siginfo_t union are only reachable through the padding here -- this
   struct is only guest-visible for waitid(), which never touches them. */
union sigval {
    int    sival_int;
    void  *sival_ptr;
};

#ifdef __APPLE__
typedef struct {
    int    si_signo;
    int    si_errno;
    int    si_code;
    int    si_pid;
    int    si_uid;
    int    si_status;
    char   __si_pad[104 - 6 * sizeof(int)];
} siginfo_t;
#else
typedef struct {
    int    si_signo;
    int    si_errno;
    int    si_code;
    int    __si_pad0;
    int    si_pid;
    int    si_uid;
    int    si_status;
    char   __si_pad[128 - 7 * sizeof(int)];
} siginfo_t;
#endif

/* si_code values for SIGCHLD, as reported by waitid(). Identical on macOS
   and Linux. */
#define CLD_EXITED    1
#define CLD_KILLED    2
#define CLD_DUMPED    3
#define CLD_TRAPPED   4
#define CLD_STOPPED   5
#define CLD_CONTINUED 6

/* sa_sigaction / SA_SIGINFO (#745). Guest code can register either the
   classic one-argument handler or the three-argument form that receives a
   siginfo_t* and a (currently unmodelled) ucontext pointer -- both live at
   the same offset via an anonymous union, exactly like the real host
   struct sigaction, so the struct's size/layout is unchanged. Which
   spelling is active is determined purely by whether SA_SIGINFO is set in
   sa_flags at delivery time (src/vm.c and src/ops.c's VRAISE handler both
   check it before deciding which argument registers to populate). */
struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t sa_mask;
    int      sa_flags;
};

extern int sigaction(int sig, const struct sigaction *restrict act,
                     struct sigaction *restrict oact);
extern int sigemptyset(sigset_t *set);
extern int sigfillset(sigset_t *set);
extern int sigaddset(sigset_t *set, int signo);
extern int sigdelset(sigset_t *set, int signo);
extern int sigismember(const sigset_t *set, int signo);

/* SA_* flags (#745) -- previously unconditional macOS values, which is a
   genuine bug beyond cosmetics: real Linux SA_SIGINFO is 0x4, which
   collides exactly with the old unconditional SA_RESETHAND (0x0004).
   Verified against real macOS and Linux x86_64/aarch64 headers (Linux
   values match across x86_64/aarch64). These flags are stored per-slot and
   round-tripped through sigaction()'s oact faithfully, but only
   SA_SIGINFO is enforced at dispatch (see src/vm.c, src/ops.c); the rest
   remain inert -- not passed to the host sigaction(). */
#ifdef __APPLE__
#define SA_RESTART    0x0002
#define SA_RESETHAND  0x0004
#define SA_NOCLDSTOP  0x0008
#define SA_NODEFER    0x0010
#define SA_NOCLDWAIT  0x0020
#define SA_SIGINFO    0x0040
#else
#define SA_NOCLDSTOP  0x00000001
#define SA_NOCLDWAIT  0x00000002
#define SA_SIGINFO    0x00000004
#define SA_RESTART    0x10000000
#define SA_NODEFER    0x40000000
#define SA_RESETHAND  0x80000000
#endif

/* SI_USER: si_code value for a signal sent via kill()/raise() rather than
   generated by the kernel. Verified against real macOS and Linux
   x86_64/aarch64 headers. */
#ifdef __APPLE__
#define SI_USER 0x10001
#else
#define SI_USER 0
#endif

extern void (*signal(int sig, void (*func)(int)))(int);
extern int raise(int sig);

#endif /* __SIGNAL_H */
