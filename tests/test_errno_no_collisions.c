// #813: errno.h's platform-varying codes are now derived from the real
// host <errno.h> (see init_errno_macros() in src/preprocess.c) instead of
// a hand-maintained #ifdef __APPLE__ table. If that derivation ever
// produced a wrong or duplicate value for two different codes, this test
// catches it -- an O(n^2) runtime distinctness check, since these values
// come from a data array, not switch/case labels (see #815, which added
// compile-time diagnostics for duplicate case values but doesn't apply here).
//
// EWOULDBLOCK is deliberately omitted: POSIX defines it identically to
// EAGAIN on every supported platform, so including both here would be a
// real (expected, not a bug) collision. Same for EOPNOTSUPP on Linux,
// where glibc defines it identically to ENOTSUP -- omitted so this test
// means the same thing on both platforms.
#include <errno.h>

static const struct { const char *name; int value; } codes[] = {
    {"EPERM", EPERM}, {"ENOENT", ENOENT}, {"ESRCH", ESRCH}, {"EINTR", EINTR},
    {"EIO", EIO}, {"ENXIO", ENXIO}, {"E2BIG", E2BIG}, {"ENOEXEC", ENOEXEC},
    {"EBADF", EBADF}, {"ECHILD", ECHILD}, {"ENOMEM", ENOMEM},
    {"EACCES", EACCES}, {"EFAULT", EFAULT}, {"EBUSY", EBUSY},
    {"EEXIST", EEXIST}, {"EXDEV", EXDEV}, {"ENODEV", ENODEV},
    {"ENOTDIR", ENOTDIR}, {"EISDIR", EISDIR}, {"EINVAL", EINVAL},
    {"ENFILE", ENFILE}, {"EMFILE", EMFILE}, {"ENOTTY", ENOTTY},
    {"EFBIG", EFBIG}, {"ENOSPC", ENOSPC}, {"ESPIPE", ESPIPE},
    {"EROFS", EROFS}, {"EMLINK", EMLINK}, {"EPIPE", EPIPE}, {"EDOM", EDOM},
    {"ERANGE", ERANGE},
    {"EAGAIN", EAGAIN}, {"EDEADLK", EDEADLK}, {"EINPROGRESS", EINPROGRESS},
    {"EALREADY", EALREADY}, {"ENOTSOCK", ENOTSOCK},
    {"EDESTADDRREQ", EDESTADDRREQ}, {"EMSGSIZE", EMSGSIZE},
    {"EPROTOTYPE", EPROTOTYPE}, {"ENOPROTOOPT", ENOPROTOOPT},
    {"ENOTSUP", ENOTSUP}, {"EAFNOSUPPORT", EAFNOSUPPORT},
    {"EADDRINUSE", EADDRINUSE}, {"EADDRNOTAVAIL", EADDRNOTAVAIL},
    {"ENETDOWN", ENETDOWN}, {"ENETUNREACH", ENETUNREACH},
    {"ECONNABORTED", ECONNABORTED}, {"ECONNRESET", ECONNRESET},
    {"ENOBUFS", ENOBUFS}, {"EISCONN", EISCONN}, {"ENOTCONN", ENOTCONN},
    {"ETIMEDOUT", ETIMEDOUT}, {"ECONNREFUSED", ECONNREFUSED},
    {"ELOOP", ELOOP}, {"ENAMETOOLONG", ENAMETOOLONG},
    {"EHOSTUNREACH", EHOSTUNREACH}, {"ENOTEMPTY", ENOTEMPTY},
    {"ENOSYS", ENOSYS}, {"ECANCELED", ECANCELED}, {"EIDRM", EIDRM},
    {"ENOMSG", ENOMSG}, {"EOVERFLOW", EOVERFLOW}, {"EBADMSG", EBADMSG},
    {"EMULTIHOP", EMULTIHOP}, {"EILSEQ", EILSEQ}, {"ENOLINK", ENOLINK},
    {"EPROTO", EPROTO}, {"ENOLCK", ENOLCK},
    {"ENOTRECOVERABLE", ENOTRECOVERABLE}, {"EOWNERDEAD", EOWNERDEAD},
    {"ESTALE", ESTALE}, {"EDQUOT", EDQUOT}, {"ETXTBSY", ETXTBSY},
    {"ENOTBLK", ENOTBLK},
};

int main(void) {
    int n = (int)(sizeof(codes) / sizeof(codes[0]));
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (codes[i].value == codes[j].value)
                return 1; /* collision between two logically distinct codes */
    return 42;
}
