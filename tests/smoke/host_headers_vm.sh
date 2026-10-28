# Programs written against the host's own headers (--sysroot ... --use-system-headers)
# run in the VM and as -c=native: sockets, unistd, fopen's SDK variants and
# glibc's scanf/strtol redirects bind to the registered natives, and the native
# TU sees the same feature-test macros as the parse (fd_set's member name).
# Skipped when no system headers are installed.
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

cat >vm_host.c <<'C'
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    int sv[2], n = 0;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
        return 1;
    if (write(sv[0], "x", 1) != 1)
        return 2;
    FILE *f = fopen("/dev/null", "r");
    if (!f)
        return 3;
    fclose(f);
    if (sscanf("7", "%d", &n) != 1 || n != 7)
        return 4;
    if (strtol("0b11", 0, 0) != 3)
        return 5;
    fprintf(stderr, "to-stderr\n");
    return 42;
}
C

cat >fdset.c <<'C'
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    int    sv[2];
    fd_set rd;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) || write(sv[0], "x", 1) != 1)
        return 1;
    FD_ZERO(&rd);
    FD_SET(sv[1], &rd);
    if (select(sv[1] + 1, &rd, 0, 0, 0) != 1 || !FD_ISSET(sv[1], &rd))
        return 2;
    FD_CLR(sv[1], &rd);
    return FD_ISSET(sv[1], &rd) ? 3 : 42;
}
C

expect_exit 42 "$CCCC" $FLAGS vm_host.c
grep -q to-stderr err.txt || fail "stderr missing"

expect_exit 42 "$CCCC" $FLAGS fdset.c
expect_exit 0 "$CCCC" $FLAGS -c=native -o fdset_native fdset.c
expect_exit 42 ./fdset_native

# On glibc the native TU carries the feature-test macros of the parse; -U drops one.
if [ "$(uname -s)" = Linux ]; then
    expect_exit 0 "$CCCC" -m fdset.c
    grep -q '^#define _XOPEN_SOURCE 700$' out.txt || fail "_XOPEN_SOURCE not forwarded"
    expect_exit 0 "$CCCC" -U_XOPEN_SOURCE -m fdset.c
    grep -q '_XOPEN_SOURCE' out.txt && fail "-U_XOPEN_SOURCE still forwarded"
fi
exit 0
