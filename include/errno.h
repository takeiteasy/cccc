/* errno.h - error codes for CCCC C compiler */

#ifndef __ERRNO_H
#define __ERRNO_H

/* #1021/#1023: see include/fenv.h's matching comment -- this exact file is
 * also what a native/generated re-emission's replayed `#include <errno.h>`
 * resolves to (-I./include is searched ahead of the system dirs), but a
 * real host compiler reprocessing the CCCC-flavored content below hits a
 * `static` __cccc_errno_ptr redefinition conflicting with the `extern`
 * declared here (native_accessor_shims, src/serialize.c, always emits its
 * own `static` definition once the accessor is used). __CCCC__ is absent
 * only when a genuine host compiler is reading this physical file, which
 * only happens during serializer replay -- hand off to #include_next for
 * the host's own, self-contained <errno.h> in that case. The E* numeric
 * values below are already baked into the AST as plain integer literals by
 * the time any of this runs (CCCC's own guest-side parsing constant-folds
 * every use), so skipping their (re)definition here costs nothing; `errno`
 * itself is declared by the host's real header instead of CCCC's own
 * `(*__cccc_errno_ptr())` macro, and __cccc_errno_ptr's own body (already
 * emitted unconditionally by native_accessor_shims whenever it's used)
 * reads that real, host-declared `errno` correctly, with no leftover
 * `extern` from this file for it to conflict with.
 * Spelled `#ifdef __CCCC__` (not `#if !defined(__CCCC__) ... #else`) so
 * tools/audit_ffi.py's guard-presence check sees a plain, nameable
 * condition it can whitelist (GUEST_ONLY_DECL_GUARDS), the same way it
 * already does for __STDC_IEC_60559_DFP__. */
#ifdef __CCCC__

#ifdef _WIN32
/* Windows FFI registration doesn't wire up __cccc_errno_ptr (POSIX-only
 * stdlib, not a tested CCCC target -- see man/COVERAGE.md); errno stays a
 * plain, host-disconnected guest global here as before. */
extern int errno;
#else
/* errno aliases the host's real per-thread errno (via an accessor function,
 * same pattern as stdin/stdout/stderr in stdio.h) so it reflects the actual
 * outcome of host-backed POSIX/libc calls instead of being an inert,
 * always-zero guest global (#736). */
extern int *__cccc_errno_ptr(void);
#define errno (*__cccc_errno_ptr())
#endif

/* Standard C error codes */
#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EINTR    4
#define EIO      5
#define ENXIO    6
#define E2BIG    7
#define ENOEXEC  8
#define EBADF    9
#define ECHILD   10
#define ENOMEM   12
#define EACCES   13
#define EFAULT   14
#define EBUSY    16
#define EEXIST   17
#define EXDEV    18
#define ENODEV   19
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define ENFILE   23
#define EMFILE   24
#define ENOTTY   25
#define EFBIG    27
#define ENOSPC   28
#define ESPIPE   29
#define EROFS    30
#define EMLINK   31
#define EPIPE    32
#define EDOM     33
#define ERANGE   34

/* POSIX error codes that differ across platforms. Values come from the
 * real host <errno.h> this binary was compiled against (see
 * init_errno_macros() in src/preprocess.c), not a hand-maintained
 * per-platform table -- #779 was a hardcoded-value bug in exactly that
 * kind of table (EDEADLK/EAGAIN swapped between macOS/glibc); #813
 * eliminates the table entirely so a future transcription slip can't
 * reintroduce the same class of bug. */
#define EAGAIN          __CCCC_EAGAIN__
#define EDEADLK         __CCCC_EDEADLK__
#define EWOULDBLOCK     __CCCC_EWOULDBLOCK__
#define EINPROGRESS     __CCCC_EINPROGRESS__
#define EALREADY        __CCCC_EALREADY__
#define ENOTSOCK        __CCCC_ENOTSOCK__
#define EDESTADDRREQ    __CCCC_EDESTADDRREQ__
#define EMSGSIZE        __CCCC_EMSGSIZE__
#define EPROTOTYPE      __CCCC_EPROTOTYPE__
#define ENOPROTOOPT     __CCCC_ENOPROTOOPT__
#define ENOTSUP         __CCCC_ENOTSUP__
#define EAFNOSUPPORT    __CCCC_EAFNOSUPPORT__
#define EADDRINUSE      __CCCC_EADDRINUSE__
#define EADDRNOTAVAIL   __CCCC_EADDRNOTAVAIL__
#define ENETDOWN        __CCCC_ENETDOWN__
#define ENETUNREACH     __CCCC_ENETUNREACH__
#define ECONNABORTED    __CCCC_ECONNABORTED__
#define ECONNRESET      __CCCC_ECONNRESET__
#define ENOBUFS         __CCCC_ENOBUFS__
#define EISCONN         __CCCC_EISCONN__
#define ENOTCONN        __CCCC_ENOTCONN__
#define ETIMEDOUT       __CCCC_ETIMEDOUT__
#define ECONNREFUSED    __CCCC_ECONNREFUSED__
#define ELOOP           __CCCC_ELOOP__
#define ENAMETOOLONG    __CCCC_ENAMETOOLONG__
#define EHOSTUNREACH    __CCCC_EHOSTUNREACH__
#define ENOTEMPTY       __CCCC_ENOTEMPTY__
#define ENOSYS          __CCCC_ENOSYS__
#define ECANCELED       __CCCC_ECANCELED__
#define EIDRM           __CCCC_EIDRM__
#define ENOMSG          __CCCC_ENOMSG__
#define EOVERFLOW       __CCCC_EOVERFLOW__
#define EBADMSG         __CCCC_EBADMSG__
#define EMULTIHOP       __CCCC_EMULTIHOP__
#define EILSEQ          __CCCC_EILSEQ__
#define ENOLINK         __CCCC_ENOLINK__
#define EPROTO          __CCCC_EPROTO__
#define ENOLCK          __CCCC_ENOLCK__
#define EOPNOTSUPP      __CCCC_EOPNOTSUPP__
#define ENOTRECOVERABLE __CCCC_ENOTRECOVERABLE__
#define EOWNERDEAD      __CCCC_EOWNERDEAD__
#define ESTALE          __CCCC_ESTALE__
#define EDQUOT          __CCCC_EDQUOT__
#define ETXTBSY         __CCCC_ETXTBSY__
#define ENOTBLK         __CCCC_ENOTBLK__

#else
#include_next <errno.h>
#endif /* __CCCC__ */

#endif /* __ERRNO_H */
