/* signal.h - signal handling declarations for CCCC */

#ifndef __SIGNAL_H
#define __SIGNAL_H

#include "stddef.h" /* offsetof */

typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int)) - 1)

/* Standard POSIX signals. Numbers differ between Darwin and Linux past the
   common core (1-15), so the rest are guarded per-platform. */
#define SIGHUP  1    /* hangup */
#define SIGINT  2    /* interrupt */
#define SIGQUIT 3    /* quit */
#define SIGILL  4    /* illegal instruction */
#define SIGTRAP 5    /* trace/debugger trap */
#define SIGABRT 6    /* abort */
#define SIGFPE  8    /* floating-point exception */
#define SIGKILL 9    /* kill (cannot be caught or ignored) */
#define SIGPIPE 13   /* broken pipe */
#define SIGALRM 14   /* alarm clock */
#define SIGTERM 15   /* termination */

#ifdef __APPLE__
#define SIGBUS    10 /* bus error */
#define SIGSEGV   11 /* segmentation fault */
#define SIGSYS    12 /* bad system call */
#define SIGURG    16 /* urgent condition on socket */
#define SIGSTOP   17 /* stop (cannot be caught or ignored) */
#define SIGTSTP   18 /* stop signal from tty */
#define SIGCONT   19 /* continue after stop */
#define SIGCHLD   20 /* child status change */
#define SIGTTIN   21 /* background tty read */
#define SIGTTOU   22 /* background tty write */
#define SIGIO     23 /* I/O now possible */
#define SIGXCPU   24 /* CPU time limit exceeded */
#define SIGXFSZ   25 /* file size limit exceeded */
#define SIGVTALRM 26 /* virtual time alarm */
#define SIGPROF   27 /* profiling time alarm */
#define SIGWINCH  28 /* window size change */
#define SIGUSR1   30 /* user-defined signal 1 */
#define SIGUSR2   31 /* user-defined signal 2 */
#else
#define SIGBUS    7  /* bus error */
#define SIGSEGV   11 /* segmentation fault */
#define SIGUSR1   10 /* user-defined signal 1 */
#define SIGUSR2   12 /* user-defined signal 2 */
#define SIGCHLD   17 /* child status change */
#define SIGCONT   18 /* continue after stop */
#define SIGSTOP   19 /* stop (cannot be caught or ignored) */
#define SIGTSTP   20 /* stop signal from tty */
#define SIGTTIN   21 /* background tty read */
#define SIGTTOU   22 /* background tty write */
#define SIGURG    23 /* urgent condition on socket */
#define SIGXCPU   24 /* CPU time limit exceeded */
#define SIGXFSZ   25 /* file size limit exceeded */
#define SIGVTALRM 26 /* virtual time alarm */
#define SIGPROF   27 /* profiling time alarm */
#define SIGWINCH  28 /* window size change */
#define SIGIO     29 /* I/O now possible */
#define SIGSYS    31 /* bad system call */
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
    int   sival_int;
    void *sival_ptr;
};

/* si_addr (#1277) -- the faulting address, reported for SIGSEGV/SIGBUS/
   SIGILL/SIGFPE. On both platforms it is a different arm of the same union
   as si_pid/si_uid/si_status (the SIGCHLD arm), so it is modelled as an
   overlapping member at the real offset rather than appended: macOS lays
   si_addr right after si_status at offset 24; glibc overlays it on the
   SIGCHLD arm at offset 16. Sizes stay the full host sizeof (104 / 128)
   with explicit trailing padding, as before. */
#ifdef __APPLE__
typedef struct {
    int   si_signo;
    int   si_errno;
    int   si_code;
    int   si_pid;
    int   si_uid;
    int   si_status;
    void *si_addr;
    char  __si_pad[104 - 24 - sizeof(void *)];
} siginfo_t;
_Static_assert(sizeof(siginfo_t) == 104, "macOS siginfo_t layout mismatch");
_Static_assert(offsetof(siginfo_t, si_status) == 20,
               "macOS siginfo_t si_status offset mismatch");
_Static_assert(offsetof(siginfo_t, si_addr) == 24,
               "macOS siginfo_t si_addr offset mismatch");
#else
typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    int __si_pad0;
    union {
        struct {
            int si_pid;
            int si_uid;
            int si_status;
        };
        void *si_addr;
    };
    char __si_pad[128 - 16 - 16];
} siginfo_t;
_Static_assert(sizeof(siginfo_t) == 128, "glibc siginfo_t layout mismatch");
_Static_assert(offsetof(siginfo_t, si_pid) == 16,
               "glibc siginfo_t si_pid offset mismatch");
_Static_assert(offsetof(siginfo_t, si_status) == 24,
               "glibc siginfo_t si_status offset mismatch");
_Static_assert(offsetof(siginfo_t, si_addr) == 16,
               "glibc siginfo_t si_addr offset mismatch");
#endif

/* struct sigevent (#804, #805, #870) -- used by aio.h's aiocb.aio_sigevent
   and mqueue.h's mq_notify() to describe how completion/message-arrival is
   reported. Layout diverges between hosts (verified against the macOS SDK
   and glibc's bits/types/sigevent_t.h in both Linux containers, including
   offsetof() of every field -- see below):
     macOS:  { int sigev_notify; int sigev_signo; union sigval sigev_value;
               void (*sigev_notify_function)(union sigval);
               void *sigev_notify_attributes; }                   (32 bytes)
     glibc:  { union sigval sigev_value; int sigev_signo;
               int sigev_notify;
               void (*sigev_notify_function)(union sigval);
               void *sigev_notify_attributes;
               <32 bytes of trailing pad to round _sigev_un out
                to its full union size> }                         (64 bytes)
   sigev_notify_attributes is declared void* rather than pthread_attr_t*
   to avoid pulling in pthread.h from here; the pointee type doesn't affect
   layout and CCCC's wrappers only ever pass NULL through it. SIGEV_THREAD
   is honored by aio_read/aio_write/aio_fsync/lio_listio/mq_notify (see
   sigevent_prepare() in src/stdlib/posix.c): the guest
   sigev_notify_function is invoked from the VM's dispatch-loop safe point
   (the same mechanism that delivers signals) once the host notification
   thread has fired, not concurrently on that host thread. */
#ifdef __APPLE__
struct sigevent {
    int          sigev_notify;
    int          sigev_signo;
    union sigval sigev_value;
    void (*sigev_notify_function)(union sigval);
    void *sigev_notify_attributes;
};
_Static_assert(sizeof(struct sigevent) == 32, "macOS sigevent layout mismatch");
_Static_assert(offsetof(struct sigevent, sigev_notify_function) == 16,
               "macOS sigevent sigev_notify_function offset mismatch");
_Static_assert(offsetof(struct sigevent, sigev_notify_attributes) == 24,
               "macOS sigevent sigev_notify_attributes offset mismatch");

#define SIGEV_NONE   0
#define SIGEV_SIGNAL 1
#define SIGEV_THREAD 3
#else
struct sigevent {
    union sigval sigev_value;
    int          sigev_signo;
    int          sigev_notify;
    void (*sigev_notify_function)(union sigval);
    void *sigev_notify_attributes;
    char  __sigev_pad[32]; /* rest of _sigev_un, unused */
};
_Static_assert(sizeof(struct sigevent) == 64, "glibc sigevent layout mismatch");
_Static_assert(offsetof(struct sigevent, sigev_notify_function) == 16,
               "glibc sigevent sigev_notify_function offset mismatch");
_Static_assert(offsetof(struct sigevent, sigev_notify_attributes) == 24,
               "glibc sigevent sigev_notify_attributes offset mismatch");

#define SIGEV_SIGNAL 0
#define SIGEV_NONE   1
#define SIGEV_THREAD 2
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

/* sigprocmask() how-values. macOS: 1/2/3; Linux: 0/1/2. Verified against
   real macOS and Linux x86_64/aarch64 headers (Linux values match across
   x86_64/aarch64). */
#ifdef __APPLE__
#define SIG_BLOCK   1
#define SIG_UNBLOCK 2
#define SIG_SETMASK 3
#else
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#endif

extern int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

/* SA_* flags (#745) -- previously unconditional macOS values, which is a
   genuine bug beyond cosmetics: real Linux SA_SIGINFO is 0x4, which
   collides exactly with the old unconditional SA_RESETHAND (0x0004).
   Verified against real macOS and Linux x86_64/aarch64 headers (Linux
   values match across x86_64/aarch64). These flags are stored per-slot and
   round-tripped through sigaction()'s oact faithfully, but only
   SA_SIGINFO is enforced at dispatch (see src/vm.c, src/ops.c); the rest
   remain inert -- not passed to the host sigaction(). */
#ifdef __APPLE__
#define SA_RESTART   0x0002
#define SA_RESETHAND 0x0004
#define SA_NOCLDSTOP 0x0008
#define SA_NODEFER   0x0010
#define SA_NOCLDWAIT 0x0020
#define SA_SIGINFO   0x0040
#else
#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000
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
