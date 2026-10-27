# Real fortified system headers (macOS SDK, glibc with -O1 -D_FORTIFY_SOURCE=2)
# route calls to __*_chk: in-bounds calls run, an overflow is caught by the
# bounds table in the VM (--checked-pointers) and by the host's fortify under
# -c=native. Skipped when neither header set is installed.
. "$(dirname "$0")/lib.sh"

if command -v xcrun >/dev/null 2>&1 && SDK=$(xcrun --show-sdk-path 2>/dev/null) &&
    [ -f "$SDK/usr/include/secure/_string.h" ]; then
    FLAGS="--sysroot $SDK --use-system-headers -D_FORTIFY_SOURCE=2"
elif [ -f /usr/include/features.h ] && [ -f /usr/include/bits/string_fortified.h ] ||
    [ -f "/usr/include/$(uname -m)-linux-gnu/bits/string_fortified.h" ]; then
    FLAGS="--sysroot / --use-system-headers -O1 -D_FORTIFY_SOURCE=2"
else
    echo "skip: no fortified system headers"
    exit 0
fi

cat >ok.c <<'C'
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
    char a[16], b[16] = "hello";
    int  fds[2];
    memcpy(a, b, sizeof a);
    memset(a, 0, sizeof a);
    strcpy(a, b);
    strncpy(a, b, 4);
    strcat(a, "x");
    snprintf(a, sizeof a, "%d", 5);
    sprintf(a, "%d", 5);
    if (pipe(fds))
        return 1;
    write(fds[1], "abcd", 4);
    read(fds[0], a, sizeof a);
    return 42;
}
C
# The SDK's unistd.h cannot run in the VM, so the VM sees the string and stdio
# calls only.
cat >vm.c <<'C'
#include <stdio.h>
#include <string.h>
int main(void) {
    char a[16], b[16] = "hello";
    memcpy(a, b, sizeof a);
    memset(a, 0, sizeof a);
    strcpy(a, b);
    strncpy(a, b, 4);
    strcat(a, "x");
    snprintf(a, sizeof a, "%d", 5);
    sprintf(a, "%d", 5);
    char big[128];
    snprintf(big, sizeof big, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
             1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18);
    return strlen(big) == 44 ? 42 : 1;
}
C
cat >overflow.c <<'C'
#include <string.h>
int main(void) {
    char a[4], b[16] = "0123456789";
    volatile unsigned long n = 8;
    memcpy(a, b, n);
    return 42;
}
C

expect_exit 42 "$CCCC" $FLAGS vm.c
expect_exit 42 "$CCCC" $FLAGS --checked-pointers vm.c
expect_exit 0 "$CCCC" $FLAGS -c=native -o ok_native ok.c
expect_exit 42 ./ok_native

expect_exit 255 "$CCCC" $FLAGS --checked-pointers overflow.c
expect_exit 0 "$CCCC" $FLAGS --checked-pointers -c=native -o checked_native overflow.c
expect_exit 255 ./checked_native

# The host's failure signal differs: SIGABRT on glibc, SIGTRAP on Darwin.
expect_exit 0 "$CCCC" $FLAGS -c=native -o fortify_native overflow.c
{ ./fortify_native; } >/dev/null 2>&1
case $? in
    133 | 134) ;;
    *) fail "host fortify did not abort the overflow" ;;
esac
