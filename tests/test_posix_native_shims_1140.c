// CCCC_FLAGS: --posix-emulation
//
// #1140: ppoll/sched_getscheduler (gated by --posix-emulation) and
// gethostbyname_r/gethostbyaddr_r/getnetbyname_r/isalpha_l/toupper_l/
// nl_langinfo_l/strfmon_l (ungated) round-tripped on the VM but failed to
// compile under -c=native on macOS -- either "use of undeclared identifier"
// (ppoll, sched_getscheduler, the _r resolver family: no host primitive at
// all) or the same, less obviously, for the four "_l" locale functions
// (they DO exist on macOS, just behind <xlocale.h>, which a plain
// <ctype.h>/<langinfo.h>/<monetary.h> #include doesn't pull in).
//
// Fixed with a native-mode <xlocale.h> injection for the "_l" family and a
// serialize_posix_compat_shims() (src/serialize.c) porting the VM's own
// ppoll/sched_* emulation and the portable _r resolver shim into the
// emitted C, guarded `#if !defined(__linux__)` so Linux keeps calling the
// real glibc symbols unchanged.
//
// Deliberately does NOT #include <pthread.h> -- combining it with <sched.h>
// used to hit the unrelated #1143 (now fixed: CCCC's own bundled include
// dirs are demoted to `-idirafter` when forwarded to the host compiler, so
// a real host <pthread.h> reached that way no longer collides with this
// file's own <sched.h>/<locale.h> -- see test_native_bundled_include_1143.c
// for that regression test). This file's own point is to prove the nine
// symbols round-trip; pthread.h stays out of scope here regardless.
//
// Deliberately does NOT use POLLWRNORM/POLLWRBAND in the ppoll test --
// CCCC's canonical POLLWRNORM/POLLWRBAND numbering is translated to the
// host's real bits by the VM (guest_to_host_pollev, src/stdlib/posix_poll.c)
// but NOT by -c=native for plain poll() either, so exercising it here would
// conflate this fix with that separate, filed follow-up.
#include <ctype.h>
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <monetary.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    // ppoll
    int fds[2];
    if (pipe(fds) != 0)
        return 1;
    if (write(fds[1], "x", 1) != 1)
        return 2;
    struct pollfd pfd  = {0};
    pfd.fd             = fds[0];
    pfd.events         = POLLIN;
    struct timespec ts = {1, 0};
    if (ppoll(&pfd, 1, &ts, NULL) != 1)
        return 3;
    if (!(pfd.revents & POLLIN))
        return 4;
    char c;
    if (read(fds[0], &c, 1) != 1)
        return 5;
    struct pollfd pfd2  = {0};
    pfd2.fd             = fds[0];
    pfd2.events         = POLLIN;
    struct timespec ts2 = {0, 50000000}; // 50ms, nothing to read
    if (ppoll(&pfd2, 1, &ts2, NULL) != 0)
        return 6;
    close(fds[0]);
    close(fds[1]);

    // sched_getscheduler
    int r = sched_getscheduler(0);
#ifdef __linux__
    if (r < 0)
        return 7;
#else
    if (r != -1 || errno != ENOSYS)
        return 8;
#endif

    // gethostbyname_r: successful lookup, deep copy proven by pointers
    // landing inside the caller's own buffer.
    struct hostent  ret;
    char            buf[2048];
    struct hostent *result = 0;
    int             herr   = 0;
    if (gethostbyname_r("localhost", &ret, buf, sizeof(buf), &result, &herr) !=
        0)
        return 9;
    if (!result || result != &ret)
        return 10;
    if ((char *)ret.h_name < buf || (char *)ret.h_name >= buf + sizeof(buf))
        return 11;

    // gethostbyname_r: short buffer -> ERANGE, *result cleared.
    char            tiny[4];
    struct hostent  ret2;
    struct hostent *result2 = (struct hostent *)1;
    if (gethostbyname_r("localhost", &ret2, tiny, sizeof(tiny), &result2,
                        &herr) != ERANGE)
        return 12;
    if (result2 != 0)
        return 13;

    // gethostbyaddr_r
    struct in_addr a;
    a.s_addr = htonl(0x7f000001); // 127.0.0.1
    struct hostent  aret;
    char            abuf[2048];
    struct hostent *aresult = 0;
    if (gethostbyaddr_r(&a, sizeof(a), AF_INET, &aret, abuf, sizeof(abuf),
                        &aresult, &herr) != 0)
        return 14;
    if (!aresult || aresult != &aret)
        return 15;

    // getnetbyname_r: tolerate "not found" (empty networks DB is common),
    // but the ERANGE/*result contract must hold either way.
    struct netent  nret;
    char           nbuf[512];
    struct netent *nresult = (struct netent *)1;
    int            nrc =
        getnetbyname_r("loopback", &nret, nbuf, sizeof(nbuf), &nresult, &herr);
    if (nrc != 0)
        return 16;
    if (nresult && nresult != &nret)
        return 17;

    // isalpha_l/toupper_l/nl_langinfo_l/strfmon_l against the C locale.
    locale_t all = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    if (!all)
        return 18;
    if (!isalpha_l('a', all))
        return 19;
    if (isalpha_l('1', all))
        return 20;
    if (toupper_l('a', all) != 'A')
        return 21;
    if (strcmp(nl_langinfo_l(RADIXCHAR, all), ".") != 0)
        return 22;
    char    mbuf[64] = {0};
    ssize_t n        = strfmon_l(mbuf, sizeof(mbuf), all, "%n", 1234.56);
    if (n < 0)
        return 23;
    freelocale(all);

    printf("posix native shims ok\n");
    return 42;
}
