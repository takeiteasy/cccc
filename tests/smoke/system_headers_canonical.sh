# Under real libc headers (--use-system-headers) the program holds the host's
# _SC_/_PC_/_CS_/LC_/POLL/SCHED_ numbers and struct layouts, and the natives
# take them unchanged. Skipped when no system headers are installed.
. "$(dirname "$0")/lib.sh"

if command -v xcrun >/dev/null 2>&1 && SDK=$(xcrun --show-sdk-path 2>/dev/null) &&
    [ -f "$SDK/usr/include/unistd.h" ]; then
    FLAGS="--sysroot $SDK --use-system-headers"
elif [ -f /usr/include/unistd.h ]; then
    FLAGS="--sysroot / --use-system-headers"
else
    echo "skip: no system headers"
    exit 0
fi

cat >prog.c <<'C'
#include <locale.h>
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <sys/statvfs.h>
#include <unistd.h>

int main(void) {
    char buf[256];
    if (sysconf(_SC_PAGESIZE) <= 0)
        return 1;
    if (pathconf("/", _PC_NAME_MAX) <= 0)
        return 2;
    if (confstr(_CS_PATH, buf, sizeof buf) == 0)
        return 3;
    if (!setlocale(LC_ALL, "C"))
        return 4;
    int fds[2];
    if (pipe(fds))
        return 5;
    struct pollfd p = {.fd = fds[1], .events = POLLOUT};
    if (poll(&p, 1, 0) != 1 || !p.revents)
        return 6;
    if (sched_get_priority_max(SCHED_RR) <= 0)
        return 7;
    struct statvfs sv;
    if (statvfs("/", &sv) != 0 || sv.f_bsize == 0)
        return 8;
    return 42;
}
C

expect_exit 42 "$CCCC" $FLAGS prog.c
expect_exit 0 "$CCCC" $FLAGS -c=native -o prog_native prog.c
expect_exit 42 ./prog_native

# The bundled headers (no --sysroot) keep the canonical numbering.
expect_exit 42 "$CCCC" prog.c
