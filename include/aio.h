/* aio.h - POSIX asynchronous I/O for CCCC (#804)
 *
 * struct aiocb diverges hard between hosts -- not just field order but
 * size (80 bytes on macOS, 168 on glibc x86_64/aarch64, verified against
 * the macOS SDK and both Linux containers), so it's declared byte-exact
 * per platform, the same pattern as struct stat and FTSENT.
 *
 * All aio_*()/lio_listio() arguments are pointers (struct aiocb *,
 * arrays of struct aiocb *, struct sigevent *) -- there is no by-value
 * aggregate argument here, so none of this needs the static-inline-shim
 * marshalling ndbm.h's datum-by-value API requires.
 *
 * SIGEV_THREAD in aio_sigevent is rejected (EINVAL) by the wrappers in
 * src/stdlib/posix.c -- see signal.h's struct sigevent comment.
 *
 * Correctness assumption specific to aio: glibc's aio implementation uses
 * helper threads that read/write the guest's aiocb and aio_buf *after*
 * the FFI call that submitted the request returns, outside CCCC's GIL.
 * This is safe only because CCCC guest memory (the interpreter's linear
 * heap/stack arena) never relocates while a request is in flight -- a
 * pointer handed to the host stays valid until explicitly freed by guest
 * code, same guarantee mmap()/shmat() already rely on. Verified end-to-
 * end with an aio_write -> aio_suspend -> aio_return round trip test
 * (tests/suites/test_suite_posix.c).
 */

#ifndef __AIO_H
#define __AIO_H

#ifdef _WIN32
#error "<aio.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"
#include "signal.h"
#include "time.h"
#include "unistd.h" /* ssize_t */
#include "stddef.h" /* offsetof */

#ifdef __APPLE__
struct aiocb {
    int             aio_fildes;
    off_t           aio_offset;
    volatile void  *aio_buf;
    size_t          aio_nbytes;
    int             aio_reqprio;
    struct sigevent aio_sigevent;
    int             aio_lio_opcode;
};
_Static_assert(sizeof(struct aiocb) == 80, "macOS aiocb layout mismatch");
#else
struct aiocb {
    int             aio_fildes;
    int             aio_lio_opcode;
    int             aio_reqprio;
    int             __pad0;
    volatile void  *aio_buf;
    size_t          aio_nbytes;
    struct sigevent aio_sigevent;
    char
        __glibc_internal[32]; /* glibc's internal
                                 next/abs_prio/policy/error_code/return_value */
    off_t aio_offset;
    char  __glibc_reserved[32];
};
_Static_assert(sizeof(struct aiocb) == 168, "glibc aiocb layout mismatch");
_Static_assert(offsetof(struct aiocb, aio_offset) == 128,
               "glibc aiocb aio_offset offset mismatch");
#endif

/* aio_cancel() return values, lio_listio() operation codes, and
   lio_listio() synchronization modes -- these are NOT identical between
   hosts (verified against the macOS SDK and glibc's aio.h in both Linux
   containers). macOS uses fixed bitmask-style values; glibc numbers them
   as plain sequential enums starting at 0. Getting this wrong is a
   correctness bug, not just cosmetics -- glibc's lio_listio() rejects a
   request outright with EINVAL if aio_lio_opcode isn't one of its own
   0/1/2 values, and passing macOS's LIO_WAIT (0x2) as glibc's mode
   argument is silently glibc's own LIO_NOWAIT (1) reversed, i.e. exactly
   backwards. */
#ifdef __APPLE__
#define AIO_ALLDONE     0x1
#define AIO_CANCELED    0x2
#define AIO_NOTCANCELED 0x4

#define LIO_NOP         0x0
#define LIO_READ        0x1
#define LIO_WRITE       0x2

#define LIO_NOWAIT      0x1
#define LIO_WAIT        0x2
#else
#define AIO_CANCELED    0
#define AIO_NOTCANCELED 1
#define AIO_ALLDONE     2

#define LIO_READ        0
#define LIO_WRITE       1
#define LIO_NOP         2

#define LIO_WAIT        0
#define LIO_NOWAIT      1
#endif

extern int aio_cancel(int fd, struct aiocb *aiocbp);
extern int aio_error(const struct aiocb *aiocbp);
extern int aio_fsync(int op, struct aiocb *aiocbp);
extern int aio_read(struct aiocb *aiocbp);
extern ssize_t aio_return(struct aiocb *aiocbp);
extern int aio_suspend(const struct aiocb *const aiocblist[], int nent,
                       const struct timespec *timeoutp);
extern int aio_write(struct aiocb *aiocbp);
extern int lio_listio(int mode, struct aiocb *const aiocblist[], int nent,
                      struct sigevent *sigp);

#endif /* __AIO_H */
