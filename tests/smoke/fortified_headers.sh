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

# glibc only: the fortified wchar, poll, syslog, stdlib and unistd entry points
# resolve to VM natives, and longjmp (asm-labelled __longjmp_chk) still unwinds.
case $FLAGS in
    *"--sysroot / "*)
        cat >glibc_vm.c <<'C'
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <wchar.h>

static int vd(int fd, const char *f, ...) { va_list a; va_start(a, f); int r = vdprintf(fd, f, a); va_end(a); return r; }
static int vsw(wchar_t *b, size_t n, const wchar_t *f, ...) { va_list a; va_start(a, f); int r = vswprintf(b, n, f, a); va_end(a); return r; }

int main(void) {
    int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
    char buf[256]; wchar_t w[32], w2[32]; char path[4096];
    gid_t g[32]; CHECK(getgroups(32, g) >= 0);
    CHECK(realpath("/", path) != NULL); CHECK(strcmp(path, "/") == 0);
    char mb[16]; CHECK(wctomb(mb, L'a') == 1);
    CHECK(mbstowcs(w, "hello", 32) == 5);
    CHECK(wcstombs(buf, w, sizeof buf) == 5);
    struct pollfd p[1] = {{0, 0, 0}}; CHECK(poll(p, 1, 0) >= 0);
    char *as = NULL; CHECK(asprintf(&as, "%d-%s", 4, "x") == 3); free(as);
    int fds[2]; CHECK(pipe(fds) == 0);
    CHECK(dprintf(fds[1], "a%d", 1) == 2); CHECK(vd(fds[1], "b%d", 2) == 2);
    char rb[8] = {0}; CHECK(read(fds[0], rb, 4) == 4); CHECK(strcmp(rb, "a1b2") == 0);
    char z[8] = "secret"; explicit_bzero(z, sizeof z); CHECK(z[0] == 0);
    FILE *f = tmpfile(); fputs("abcdef\nrest\n", f); rewind(f);
    char l[16]; CHECK(fgets_unlocked(l, sizeof l, f) != NULL); CHECK(strcmp(l, "abcdef\n") == 0);
    char ch[4]; CHECK(fread_unlocked(ch, 1, 4, f) == 4); fclose(f);
    char dom[64]; CHECK(getdomainname(dom, sizeof dom) == 0);
    char lk[64]; CHECK(readlinkat(AT_FDCWD, "/proc/self/exe", lk, sizeof lk) > 0);
    char tt[64]; (void)ttyname_r(0, tt, sizeof tt);
    char lg[64]; (void)getlogin_r(lg, sizeof lg);
    char ip[INET_ADDRSTRLEN]; struct in_addr ia = {htonl(0x7f000001)};
    CHECK(inet_ntop(AF_INET, &ia, ip, sizeof ip) != NULL); CHECK(strcmp(ip, "127.0.0.1") == 0);
    syslog(LOG_DEBUG, "ignored %d", 1);
    CHECK(wmemcpy(w, L"abc", 4) == w); CHECK(wmemmove(w2, w, 4) == w2); CHECK(wmemset(w2, L'q', 2) == w2);
    CHECK(wcscpy(w, L"hello") == w); CHECK(wcscat(w, L"!") == w); CHECK(wcsncpy(w2, w, 3) == w2); CHECK(wcsncat(w2, L"zz", 1) == w2);
    CHECK(wcpcpy(w, L"ab") == w + 2); CHECK(wmempcpy(w2, w, 2) == w2 + 2);
    CHECK(swprintf(w, 32, L"%d-%ls", 5, L"x") == 3); CHECK(vsw(w2, 32, L"%d", 9) == 1);
    const char *s = "abc"; mbstate_t st; memset(&st, 0, sizeof st);
    CHECK(mbsrtowcs(w, &s, 32, &st) == 3);
    const wchar_t *ws = L"xyz"; memset(&st, 0, sizeof st);
    CHECK(wcsrtombs(buf, &ws, sizeof buf, &st) == 3);
    memset(&st, 0, sizeof st); CHECK(wcrtomb(mb, L'a', &st) == 1);
    CHECK(wcslcpy(w, L"abcdef", 4) == 6);
    CHECK(strlcpy(buf, "abcdef", 4) == 6);
    jmp_buf jb; if (setjmp(jb) == 0) longjmp(jb, 1); else CHECK(1);
    printf(fails ? "SOME FAILED\n" : "all ok\n");
    return fails ? 1 : 42;
}
C
        cat >longjmp_chk.c <<'C'
#include <setjmp.h>
int main(void) {
    jmp_buf jb;
    volatile int stage = 0;
    int r = setjmp(jb);
    if (r == 0) { stage = 1; longjmp(jb, 3); }
    sigjmp_buf sb;
    int s = sigsetjmp(sb, 1);
    if (s == 0) { stage = 2; siglongjmp(sb, 5); }
    return (r == 3 && s == 5 && stage == 2) ? 42 : 1;
}
C
        expect_exit 42 "$CCCC" $FLAGS glibc_vm.c
        expect_exit 42 "$CCCC" $FLAGS longjmp_chk.c
        expect_exit 0 "$CCCC" $FLAGS -c=native -o longjmp_native longjmp_chk.c
        expect_exit 42 ./longjmp_native
        ;;
esac
