/* mqueue.h - POSIX message queues for CCCC (#805)
 *
 * Linux-only: macOS has no mqueue implementation at all (no header, no
 * libc symbols, no POSIX-emulation-friendly fallback -- unlike sched.h's
 * process-scheduling stubs, there's nothing non-lossy to emulate for
 * message queues), so per the #824 no-lossy-emulation policy this header
 * is declared under __linux__ only; a guest that includes it on macOS
 * gets nothing declared, matching what a native compiler on that host
 * would do.
 *
 * struct mq_attr is 64 bytes on glibc (both x86_64 and aarch64,
 * verified in both Linux containers) -- only the first 32 bytes are the
 * four named `long` fields; the trailing 32 bytes are reserved padding
 * inside the real struct. mq_getattr()/mq_open() write the full host
 * struct through this pointer, so the guest struct must reserve all 64
 * bytes or the host call overflows into adjacent guest memory -- same
 * class of bug the siginfo_t comment in signal.h documents.
 */

#ifndef __MQUEUE_H
#define __MQUEUE_H

#ifdef _WIN32
#error "<mqueue.h> is only available on POSIX targets in CCCC"
#endif

#ifdef __linux__

#include "sys/types.h"
#include "time.h"
#include "signal.h"
#include "fcntl.h"

typedef int mqd_t;

struct mq_attr {
    long mq_flags;
    long mq_maxmsg;
    long mq_msgsize;
    long mq_curmsgs;
    long __reserved[4];
};

_Static_assert(sizeof(struct mq_attr) == 64, "glibc mq_attr layout mismatch");

/* mq_open()'s 3rd/4th arguments (mode_t, const struct mq_attr *) are only
   present when O_CREAT is in oflag -- same variadic shape as open(). */
extern mqd_t mq_open(const char *name, int oflag, ...);
extern int mq_close(mqd_t mqdes);
extern int mq_unlink(const char *name);
extern ssize_t mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                       unsigned int msg_prio);
extern ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                          unsigned int *msg_prio);
extern int mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                        unsigned int           msg_prio,
                        const struct timespec *abs_timeout);
extern ssize_t mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                               unsigned int          *msg_prio,
                               const struct timespec *abs_timeout);
/* mq_notify() only supports SIGEV_NONE/SIGEV_SIGNAL (see signal.h's
   struct sigevent comment) -- SIGEV_THREAD is rejected with EINVAL. */
extern int mq_notify(mqd_t mqdes, const struct sigevent *notification);
extern int mq_setattr(mqd_t mqdes, const struct mq_attr *newattr,
                      struct mq_attr *oldattr);
extern int mq_getattr(mqd_t mqdes, struct mq_attr *attr);

#endif /* __linux__ */

#endif /* __MQUEUE_H */
