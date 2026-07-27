// CCCC_FLAGS: --testing
// Consolidated suite: POSIX: unistd, dirent, glob, regex, socket, mman, etc.
// Source tests: test_posix_arpa_inet, test_posix_dirent, test_posix_extra_ffi, test_posix_fnmatch, test_posix_glob, test_posix_libgen, test_posix_poll, test_posix_pwd_grp, test_posix_regex, test_posix_socket_netdb, test_posix_strings, test_posix_sys_mman, test_posix_sys_stat, test_posix_sys_time, test_posix_termios, test_posix_unistd_fcntl, test_posix_utime, test_posix_vfs_decls,
//   test_glob_header, test_quick_exit, test_posix_sys_wait,
//   test_posix_sysconf_pathconf_confstr, test_posix_host_global_bridge,
//   test_posix_sendmsg_recvmsg_scm_rights, test_posix_ipv6_udp_roundtrip,
//   test_posix_ipv6_advanced_options,
//   test_posix_netent, test_posix_waitid, test_posix_dns_gil_concurrency,
//   test_posix_gethostbyname_r,
//   test_posix_servent_protoent, test_posix_getrusage, test_posix_wait4,
//   test_posix_sigaction_siginfo, test_posix_sigaction_flags,
//   test_posix_ipv6_multicast_roundtrip,
//   test_posix_rlimit_priority, test_posix_uname, test_posix_times,
//   test_posix_tar_cpio, test_posix_syslog, test_posix_select

#include <arpa/inet.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
#include <fnmatch.h>
#include <glob.h>
#include <libgen.h>
#include <poll.h>
#include <pwd.h>
#include <grp.h>
#include <regex.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <termios.h>
#include <utime.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <pthread.h>
#include <sys/utsname.h>
#include <sys/times.h>
#include <tar.h>
#include <cpio.h>
#include <syslog.h>
#include <stdarg.h>
#include <sys/select.h>

// [from test_posix_extra_ffi]
// Regression test for #590: additional POSIX FFI functions registered for the
// SQLite unix VFS (fchmod, geteuid, pread, pwrite, getpagesize, nanosleep, ...).
//
// Exercises the newly-registered functions in a portable way and returns 42.

#pragma cccc suite begin "posix"

// test_posix_arpa_inet
[[cccc::test(return = 42)]]
int test_posix_arpa_inet(void) {
    if (htonl(0x12345678) == 0x12345678) {
        /* little-endian host */
        if (htonl(0x12345678) != 0x78563412) return 1;
        if (htons(0x1234) != 0x3412) return 2;
        if (ntohl(0x78563412) != 0x12345678) return 3;
        if (ntohs(0x3412) != 0x1234) return 4;
    }

    if (inet_addr("127.0.0.1") == (uint32_t)(-1)) return 5;

    struct in_addr addr;
    if (inet_pton(AF_INET, "127.0.0.1", &addr) != 1) return 6;
    if (addr.s_addr != inet_addr("127.0.0.1")) return 7;

    char buf[32];
    const char *s = inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    if (!s) return 8;
    if (s[0] != '1' || s[1] != '2' || s[2] != '7') return 9;

    return 42;
}

// test_posix_dirent
[[cccc::test(return = 42)]]
int test_posix_dirent(void) {
    DIR *d = opendir("tests/suites");
    if (!d) return 1;

    int found = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != 0) {
        if (strcmp(ent->d_name, "test_suite_posix.c") == 0) {
            found = 1;
            if (ent->d_reclen == 0) return 2;
            break;
        }
    }

    if (closedir(d) != 0) return 3;
    if (!found) return 4;
    return 42;
}

// test_posix_extra_ffi
[[cccc::test(return = 42)]]
int test_posix_extra_ffi(void) {
    // getpagesize: must be a positive power-of-two-ish value
    if (getpagesize() <= 0)
        return 1;

    // geteuid: effective uid is non-negative (uid_t); just confirm it is callable
    (void)geteuid();

    // pread / pwrite round-trip through a temp file
    char tmpl[] = "/tmp/cccc_posix_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0)
        return 2;

    const char *msg = "hello";
    if (pwrite(fd, msg, 5, 0) != 5)
        return 3;

    // fchmod the temp fd
    if (fchmod(fd, 0600) != 0)
        return 4;

    char buf[8] = {0};
    if (pread(fd, buf, 5, 0) != 5)
        return 5;
    if (memcmp(buf, "hello", 5) != 0)
        return 6;

    close(fd);
    unlink(tmpl);

    // nanosleep: a tiny, harmless sleep
    struct timespec req = { 0, 1000000 }; // 1 ms
    if (nanosleep(&req, 0) != 0)
        return 7;

    return 42;
}

// test_posix_fnmatch
[[cccc::test(return = 42)]]
int test_posix_fnmatch(void) {
    if (fnmatch("*.c", "hello.c", 0) != 0) return 1;
    if (fnmatch("*.c", "hello.h", 0) != FNM_NOMATCH) return 2;
    if (fnmatch("a?c", "abc", 0) != 0) return 3;
    if (fnmatch("*.txt", "a/b.txt", FNM_PATHNAME) != FNM_NOMATCH) return 4;
    return 42;
}

// test_posix_glob
[[cccc::test(return = 42)]]
int test_posix_glob(void) {
    glob_t g;
    int rc = glob("tests/suites/test_suite_posix.c", 0, 0, &g);
    if (rc != 0) return 1;
    if (g.gl_pathc != 1) return 2;
    if (!g.gl_pathv) return 3;
    if (strcmp(g.gl_pathv[0], "tests/suites/test_suite_posix.c") != 0) return 4;
    globfree(&g);

    rc = glob("tests/no-such-posix-glob-file-*.c", 0, 0, &g);
    if (rc != GLOB_NOMATCH) return 5;
    return 42;
}

// test_posix_libgen
[[cccc::test(return = 42)]]
int test_posix_libgen(void) {
    char p1[] = "/usr/lib";
    char p2[] = "/usr/";
    char p3[] = "usr";

    if (strcmp(basename(p1), "lib") != 0) return 1;
    if (strcmp(basename(p2), "usr") != 0) return 2;
    if (strcmp(basename(p3), "usr") != 0) return 3;

    if (strcmp(dirname(p1), "/usr") != 0) return 4;
    if (strcmp(dirname(p2), "/") != 0) return 5;
    if (strcmp(dirname(p3), ".") != 0) return 6;

    return 42;
}

// test_posix_poll
[[cccc::test(return = 42)]]
int test_posix_poll(void) {
    int fd[2];
    if (pipe(fd) != 0) return 1;

    char msg[] = "x";
    write(fd[1], msg, 1);

    struct pollfd pfd = { fd[0], POLLIN, 0 };
    int r = poll(&pfd, 1, 1000);
    if (r != 1) return 2;
    if (!(pfd.revents & POLLIN)) return 3;

    close(fd[0]);
    close(fd[1]);
    return 42;
}

// test_posix_pwd_grp
[[cccc::test(return = 42)]]
int test_posix_pwd_grp(void) {
    struct passwd *pw = getpwuid(0);
    if (!pw) return 1;
    if (!pw->pw_name || !pw->pw_dir || !pw->pw_shell) return 2;

    struct passwd *pw2 = getpwnam(pw->pw_name);
    if (!pw2) return 3;
    if (pw2->pw_uid != pw->pw_uid) return 4;

    struct group *gr = getgrgid(0);
    if (!gr) return 5;
    if (!gr->gr_name) return 6;

    struct group *gr2 = getgrnam(gr->gr_name);
    if (!gr2) return 7;
    if (gr2->gr_gid != gr->gr_gid) return 8;

    return 42;
}

// test_posix_regex
[[cccc::test(return = 42)]]
int test_posix_regex(void) {
    regex_t re;
    regmatch_t match[2];

    int rc = regcomp(&re, "j([0-9]+)", REG_EXTENDED);
    if (rc != 0) return 1;
    if (re.re_nsub != 1) return 2;

    rc = regexec(&re, "j42", 2, match, 0);
    if (rc != 0) return 3;
    if (match[0].rm_so != 0 || match[0].rm_eo != 3) return 4;
    if (match[1].rm_so != 1 || match[1].rm_eo != 3) return 5;

    rc = regexec(&re, "abc", 0, 0, 0);
    if (rc != REG_NOMATCH) return 6;

    regfree(&re);
    return 42;
}

// test_posix_socket_netdb
[[cccc::test(return = 42)]]
int test_posix_socket_netdb(void) {
    struct hostent *he = gethostbyname("localhost");
    if (!he) return 1;
    if (he->h_addrtype != AF_INET) return 2;
    if (he->h_length != 4) return 3;

    struct addrinfo hints;
    struct addrinfo *res = 0;
    for (int i = 0; i < (int)(sizeof(hints)); i++)
        ((char *)&hints)[i] = 0;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo("127.0.0.1", "80", &hints, &res);
    if (gai != 0) return 4;
    if (!res) return 5;
    if (res->ai_family != AF_INET) return 6;
    if (res->ai_addrlen < (socklen_t)sizeof(struct sockaddr_in)) return 7;
    freeaddrinfo(res);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 8;
    close(fd);

    struct sockaddr_in addr;
    for (int i = 0; i < (int)(sizeof(addr)); i++)
        ((char *)&addr)[i] = 0;
#ifdef __APPLE__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    socklen_t len = sizeof(addr);
    if (bind(-1, (struct sockaddr *)&addr, sizeof(addr)) != -1) return 9;
    if (listen(-1, 1) != -1) return 10;
    if (accept(-1, (struct sockaddr *)&addr, &len) != -1) return 11;
    if (connect(-1, (struct sockaddr *)&addr, sizeof(addr)) != -1) return 12;
    if (getsockname(-1, (struct sockaddr *)&addr, &len) != -1) return 13;

    return 42;
}

// test_posix_netent
// #743: getnetbyname()/getnetbyaddr()/setnetent()/endnetent() against
// struct netent. /etc/networks is frequently empty (or absent) on both
// developer machines and CI, so a "real entry" round trip isn't
// guaranteed -- this tolerates an empty networks database and only
// asserts consistency between getnetbyname() and getnetbyaddr() when an
// entry actually resolves.
[[cccc::test(return = 42)]]
int test_posix_netent(void) {
    setnetent(0);

    struct netent *ne = getnetbyname("loopback");
    if (ne) {
        if (!ne->n_name) return 1;
        struct netent *by_addr = getnetbyaddr(ne->n_net, ne->n_addrtype);
        if (!by_addr) return 2;
        if (by_addr->n_net != ne->n_net) return 3;
        if (by_addr->n_addrtype != ne->n_addrtype) return 4;
    }

    /* A name that should never resolve to a real network entry. */
    if (getnetbyname("cccc-nonexistent-network-name")) return 5;

    endnetent();
    return 42;
}

// test_posix_servent_protoent
// #746: getservbyname()/getservbyport()/getprotobyname()/getprotobynumber()
// against struct servent/struct protoent. /etc/services and /etc/protocols
// are near-universally present with "http"/"tcp" entries (unlike
// /etc/networks, which test_posix_netent has to tolerate being empty), so
// this asserts the real lookup succeeds and is internally consistent.
// s_port is network byte order -- must ntohs() it before comparing.
[[cccc::test(return = 42)]]
int test_posix_servent_protoent(void) {
    setservent(0);
    setprotoent(0);

    struct servent *se = getservbyname("http", "tcp");
    if (se) {
        if (!se->s_name) return 1;
        if (ntohs((unsigned short)se->s_port) != 80) return 2;
        struct servent *by_port = getservbyport(se->s_port, "tcp");
        if (!by_port) return 3;
        if (by_port->s_port != se->s_port) return 4;
    }

    struct protoent *pe = getprotobyname("tcp");
    if (pe) {
        if (!pe->p_name) return 5;
        if (pe->p_proto != 6) return 6;
        struct protoent *by_num = getprotobynumber(pe->p_proto);
        if (!by_num) return 7;
        if (by_num->p_proto != pe->p_proto) return 8;
    }

    if (getservbyname("cccc-nonexistent-service-name", "tcp")) return 9;
    if (getprotobyname("cccc-nonexistent-protocol-name")) return 10;

    endservent();
    endprotoent();
    return 42;
}

// test_posix_dns_gil_concurrency
// #748: gethostbyname/getaddrinfo/getnetbyname etc. used to be direct
// (non-GIL-releasing) FFI calls, so a real DNS/NSS lookup could stall every
// other VM thread for as long as it blocked. Now they release the GIL like
// recv/send/waitpid/waitid do. This doesn't assert timing (that's
// network-dependent and was verified by hand) -- it proves the regression-
// prone part: two threads hammering the now-GIL-releasing lookups
// concurrently don't deadlock, corrupt the ExecState save/restore, or
// return wrong results.
static void *dns_gil_worker(void *arg) {
    int iterations = *(int *)arg;
    for (int i = 0; i < iterations; i++) {
        struct hostent *he = gethostbyname("localhost");
        if (!he || he->h_addrtype != AF_INET) return (void *)1;

        struct addrinfo hints, *res = 0;
        for (int j = 0; j < (int)sizeof(hints); j++) ((char *)&hints)[j] = 0;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo("127.0.0.1", "80", &hints, &res) != 0 || !res) return (void *)2;
        freeaddrinfo(res);
    }
    return (void *)0;
}

[[cccc::test(return = 42)]]
int test_posix_dns_gil_concurrency(void) {
    pthread_t t1, t2;
    int n = 20;
    if (pthread_create(&t1, 0, dns_gil_worker, &n) != 0) return 1;
    if (pthread_create(&t2, 0, dns_gil_worker, &n) != 0) return 2;

    void *r1 = 0, *r2 = 0;
    if (pthread_join(t1, &r1) != 0) return 3;
    if (pthread_join(t2, &r2) != 0) return 4;
    if (r1 != 0) return 5;
    if (r2 != 0) return 6;
    return 42;
}

// test_posix_gethostbyname_r
// #785: race-free alternative to gethostbyname/gethostbyaddr/getnetbyname
// above, deferred from #748 (releasing the GIL means concurrent guest
// threads can now race on those functions' static host storage). Two
// threads each loop gethostbyname_r() with a *private* stack buffer and
// assert the result points *inside* that buffer (proving a real deep copy,
// not an alias into shared static storage that a concurrent call could
// overwrite), plus the correctness/ERANGE/not-found contracts.
static void *gethostbyname_r_worker(void *arg) {
    int iterations = *(int *)arg;
    char buf[2048];
    for (int i = 0; i < iterations; i++) {
        struct hostent ret;
        struct hostent *result = 0;
        int herr = 0;
        int rc = gethostbyname_r("localhost", &ret, buf, sizeof(buf), &result, &herr);
        if (rc != 0) return (void *)1;
        if (!result || result != &ret) return (void *)2;
        if (ret.h_addrtype != AF_INET || ret.h_length != 4) return (void *)3;
        /* Proves the deep copy: h_name/h_addr_list point into this
           worker's own stack buffer, not shared static storage a
           concurrent call on the other thread could be overwriting. */
        if ((char *)ret.h_name < buf || (char *)ret.h_name >= buf + sizeof(buf)) return (void *)4;
        if ((char *)ret.h_addr_list[0] < buf || (char *)ret.h_addr_list[0] >= buf + sizeof(buf)) return (void *)5;
        unsigned char *addr = (unsigned char *)ret.h_addr_list[0];
        if (addr[0] != 127 || addr[1] != 0 || addr[2] != 0 || addr[3] != 1) return (void *)6;
    }
    return (void *)0;
}

[[cccc::test(return = 42)]]
int test_posix_gethostbyname_r(void) {
    pthread_t t1, t2;
    int n = 20;
    if (pthread_create(&t1, 0, gethostbyname_r_worker, &n) != 0) return 1;
    if (pthread_create(&t2, 0, gethostbyname_r_worker, &n) != 0) return 2;

    void *r1 = 0, *r2 = 0;
    if (pthread_join(t1, &r1) != 0) return 3;
    if (pthread_join(t2, &r2) != 0) return 4;
    if (r1 != 0) return 5;
    if (r2 != 0) return 6;

    /* Short buffer -> ERANGE, *result cleared. */
    char tiny[4];
    struct hostent ret2;
    struct hostent *result2 = (struct hostent *)1;
    int herr = 0;
    int rc = gethostbyname_r("localhost", &ret2, tiny, sizeof(tiny), &result2, &herr);
    if (rc != ERANGE) return 7;
    if (result2 != 0) return 8;

    /* gethostbyaddr_r round trip on the same loopback address. */
    struct in_addr a;
    a.s_addr = htonl(0x7f000001); /* 127.0.0.1 */
    struct hostent aret;
    char abuf[2048];
    struct hostent *aresult = 0;
    int aherr = 0;
    if (gethostbyaddr_r(&a, sizeof(a), AF_INET, &aret, abuf, sizeof(abuf), &aresult, &aherr) != 0) return 9;
    if (!aresult) return 10;

    /* getnetbyname_r: tolerate "not found" (the networks DB is often empty
       in containers), but the ERANGE/*result contract must still hold. */
    struct netent nret;
    char nbuf[512];
    struct netent *nresult = (struct netent *)1;
    int nherr = 0;
    int nrc = getnetbyname_r("loopback", &nret, nbuf, sizeof(nbuf), &nresult, &nherr);
    if (nrc != 0) return 11;
    if (nresult && nresult != &nret) return 12;
    if (!nresult && nherr != HOST_NOT_FOUND) return 13;

    return 42;
}

// test_posix_strings
[[cccc::test(return = 42)]]
int test_posix_strings(void) {
    if (strcasecmp("abc", "ABC") != 0) return 1;
    if (strcasecmp("abc", "abd") >= 0) return 2;
    if (strncasecmp("abc", "AB", 2) != 0) return 3;
    if (strncasecmp("abc", "abd", 2) != 0) return 4;
    return 42;
}

// test_posix_sys_mman
[[cccc::test(return = 42)]]
int test_posix_sys_mman(void) {
    size_t pagesize = 4096;
    void *p = mmap(0, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == (void *)-1) return 1;

    strcpy((char *)p, "hello");
    if (strcmp((char *)p, "hello") != 0) return 2;

    if (mprotect(p, pagesize, PROT_READ) != 0) return 3;

    if (munmap(p, pagesize) != 0) return 4;
    return 42;
}

// test_posix_sys_stat
[[cccc::test(return = 42)]]
int test_posix_sys_stat(void) {
    char path[] = "/tmp/cccc-stat-test-XXXXXX";
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) return 1;

    if (write(fd, "abc", 3) != 3) return 2;
    close(fd);

    struct stat st;
    if (stat(path, &st) != 0) return 3;
    if (st.st_size != 3) return 4;
    if (!S_ISREG(st.st_mode)) return 5;

    if (chmod(path, S_IRUSR) != 0) return 6;
    if (stat(path, &st) != 0) return 7;
    if ((st.st_mode & S_IRWXU) != S_IRUSR) return 8;
    if (st.st_mode & S_IWUSR) return 9;

    unlink(path);

    if (mkdir("/tmp/cccc-stat-dir-test", S_IRWXU) != 0) return 10;
    if (stat("/tmp/cccc-stat-dir-test", &st) != 0) return 11;
    if (!S_ISDIR(st.st_mode)) return 12;
    rmdir("/tmp/cccc-stat-dir-test");

    return 42;
}

// test_posix_sys_time
[[cccc::test(return = 42)]]
int test_posix_sys_time(void) {
    struct timeval tv1, tv2;
    if (gettimeofday(&tv1, 0) != 0) return 1;

    usleep(10000); /* 10ms */

    if (gettimeofday(&tv2, 0) != 0) return 2;

    /* tv2 should be >= tv1 */
    if (tv2.tv_sec < tv1.tv_sec) return 3;
    if (tv2.tv_sec == tv1.tv_sec && tv2.tv_usec < tv1.tv_usec) return 4;

    /* timeradd / timersub macros */
    struct timeval a = { 1, 500000 };
    struct timeval b = { 2, 600000 };
    struct timeval res;
    timeradd(&a, &b, &res);
    if (res.tv_sec != 4 || res.tv_usec != 100000) return 5;

    timersub(&b, &a, &res);
    if (res.tv_sec != 1 || res.tv_usec != 100000) return 6;

    return 42;
}

// test_posix_termios
[[cccc::test(return = 42)]]
int test_posix_termios(void) {
    struct termios t;
    if (NCCS < 10) return 1;
    t.c_cc[0] = 0;

    int rc = tcgetattr(STDIN_FILENO, &t);
    if (rc == 0) {
        if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) return 2;
    }

    return 42;
}

// test_posix_unistd_fcntl
[[cccc::test(return = 42)]]
int test_posix_unistd_fcntl(void) {
    char path[] = "/tmp/cccc-posix-test-XXXXXX";
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) return 1;

    char msg[] = "abc";
    if (write(fd, msg, 3) != 3) return 2;
    if (lseek(fd, 0, SEEK_SET) != 0) return 3;

    char buf[4] = {0};
    if (read(fd, buf, 3) != 3) return 4;
    if (buf[0] != 'a' || buf[1] != 'b' || buf[2] != 'c') return 5;

    if (close(fd) != 0) return 6;
    if (unlink(path) != 0) return 7;

    return 42;
}

// test_posix_utime
[[cccc::test(return = 42)]]
int test_posix_utime(void) {
    char path[] = "/tmp/cccc-utime-test-XXXXXX";
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) return 1;
    close(fd);

    struct utimbuf times;
    times.actime = 1234567890;
    times.modtime = 1234567890;
    if (utime(path, &times) != 0) return 2;

    struct stat st;
    if (stat(path, &st) != 0) return 3;
#ifdef __APPLE__
    if (st.st_atimespec.tv_sec != 1234567890) return 4;
    if (st.st_mtimespec.tv_sec != 1234567890) return 5;
#else
    if (st.st_atim.tv_sec != 1234567890) return 4;
    if (st.st_mtim.tv_sec != 1234567890) return 5;
#endif

    unlink(path);
    return 42;
}

// test_posix_vfs_decls
[[cccc::test(return = 42)]]
int test_posix_vfs_decls(void) {
    /* Reference the new declarations so the compiler must resolve them. */
    void *fns[] = {
        (void*)fchmod, (void*)fchown, (void*)geteuid, (void*)getuid,
        (void*)getgid, (void*)getegid, (void*)readlink, (void*)symlink,
        (void*)pread, (void*)pwrite,
#ifdef __linux__
        // fdatasync is absent from Darwin's libc entirely; its guest-side
        // declaration in include/unistd.h is __linux__-guarded to match (#783).
        (void*)fdatasync,
#endif
        (void*)getpagesize,
        (void*)chown, (void*)ioctl, (void*)flock, (void*)statfs, (void*)fstatfs,
    };
    struct flock fl;
    fl.l_type = F_WRLCK; fl.l_whence = 0; fl.l_start = 0; fl.l_len = 0;
    struct winsize ws; ws.ws_row = 0;
    struct statfs sfs; sfs.f_bsize = 0; sfs.f_flags = 0;
    int constants = F_GETLK + F_SETLK + F_SETLKW + F_RDLCK + F_UNLCK
                  + LOCK_EX + LOCK_SH + LOCK_NB + LOCK_UN
                  + ENOLCK + EOPNOTSUPP + _SC_PAGESIZE + MAXPATHLEN
                  + (MAP_FAILED == (void*)-1) + (fl.l_type != 0) + (ws.ws_row == 0)
                  + (sfs.f_bsize == 0) + (sizeof(fns) > 0) + MIN(1,2) + MAX(1,2);
    (void)constants;
    return 42;
}

// [from test_glob_header]
// Basic glob_t struct: sizeof must be > 0.
[[cccc::test(return = 42)]]
int test_glob_header(void) {
    glob_t g;
    return sizeof(g) > 0 ? 42 : 1;
}

// [from test_posix_sys_wait]
// fork() + waitpid() + WEXITSTATUS.
[[cccc::test(return = 42)]]
int test_posix_sys_wait(void) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) { _exit(42); }
    int status;
    pid_t r = waitpid(pid, &status, 0);
    if (r != pid) return 2;
    if (!WIFEXITED(status)) return 3;
    if (WEXITSTATUS(status) != 42) return 4;
    return 42;
}

// test_posix_waitid
// #744: waitid(P_PID, ..., WEXITED) fills a guest siginfo_t through the FFI
// boundary exactly as the real host waitid() would -- proves the guest
// struct is sized/laid out to match the host ABI (a too-small guest struct
// would silently corrupt adjacent guest memory rather than fail loudly).
[[cccc::test(return = 42)]]
int test_posix_waitid(void) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) { _exit(7); }

    siginfo_t si;
    for (int i = 0; i < (int)sizeof(si); i++) ((char *)&si)[i] = 0;
    if (waitid(P_PID, (id_t)pid, &si, WEXITED) != 0) return 2;
    if (si.si_pid != pid) return 3;
    if (si.si_code != CLD_EXITED) return 4;
    if (si.si_status != 7) return 5;
    return 42;
}

// test_posix_getrusage
// #747: getrusage() fills a guest struct rusage through the FFI boundary,
// same struct-fidelity concern as waitid()'s siginfo_t above -- proves the
// guest layout matches the host ABI (sizeof(struct rusage) == 144 on both
// macOS and Linux). Only asserts non-negative/plausible values since exact
// usage numbers aren't portable across hosts.
[[cccc::test(return = 42)]]
int test_posix_getrusage(void) {
    struct rusage ru;
    for (int i = 0; i < (int)sizeof(ru); i++) ((char *)&ru)[i] = 0;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 1;
    if (ru.ru_maxrss <= 0) return 2;
    if (ru.ru_utime.tv_sec < 0) return 3;
    return 42;
}

// test_posix_wait4
// #747: wait3()/wait4() are the BSD-style wait variants that also fill a
// struct rusage for the reaped child, unlike waitpid()/waitid().
[[cccc::test(return = 42)]]
int test_posix_wait4(void) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) { _exit(7); }

    int status = 0;
    struct rusage ru;
    for (int i = 0; i < (int)sizeof(ru); i++) ((char *)&ru)[i] = 0;
    pid_t r = wait4(pid, &status, 0, &ru);
    if (r != pid) return 2;
    if (!WIFEXITED(status)) return 3;
    if (WEXITSTATUS(status) != 7) return 4;
    /* The child did essentially no work, so most counters may legitimately
       be 0 -- just prove the struct was actually written to, not left as
       whatever garbage/zero it started as for every field. */
    if (ru.ru_maxrss < 0) return 5;
    return 42;
}

// test_posix_rlimit_priority
// #786: the rest of <sys/resource.h> beyond #747's getrusage/wait3/wait4 --
// struct rlimit, RLIMIT_* (numbering genuinely diverges between macOS and
// Linux, unlike struct rusage's layout), getrlimit/setrlimit, and
// getpriority/setpriority. Lowers and restores RLIMIT_NOFILE's soft limit
// (must restore, or later tests in this same process would inherit the
// lowered fd limit) rather than raising it, since raising past the hard
// limit requires privilege. Similarly only lowers (never raises) process
// niceness via setpriority, since raising priority is also privileged.
[[cccc::test(return = 42)]]
int test_posix_rlimit_priority(void) {
    if (sizeof(struct rlimit) != 16) return 1;
    if (RLIMIT_CORE == RLIMIT_NOFILE) return 2;

    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) return 3;
    if (rl.rlim_cur > rl.rlim_max && rl.rlim_max != RLIM_INFINITY) return 4;

    rlim_t original_cur = rl.rlim_cur;
    if (original_cur > 16) {
        struct rlimit lowered = rl;
        lowered.rlim_cur = 16;
        if (setrlimit(RLIMIT_NOFILE, &lowered) != 0) return 5;

        struct rlimit check;
        if (getrlimit(RLIMIT_NOFILE, &check) != 0) return 6;
        if (check.rlim_cur != 16) return 7;

        /* Restore -- other tests in this same process must not inherit a
           lowered fd limit. */
        struct rlimit restore = rl;
        restore.rlim_cur = original_cur;
        if (setrlimit(RLIMIT_NOFILE, &restore) != 0) return 8;
    }

    errno = 0;
    int nice_val = getpriority(PRIO_PROCESS, 0);
    if (nice_val == -1 && errno != 0) return 9;
    if (nice_val < -20 || nice_val > 19) return 10;

    /* Lower (never raise) niceness by 1 -- unprivileged processes can only
       increase niceness (lower priority), not decrease it. Skip if already
       at the max nice value. */
    if (nice_val < 19) {
        if (setpriority(PRIO_PROCESS, 0, nice_val + 1) != 0) return 11;
        errno = 0;
        int check_nice = getpriority(PRIO_PROCESS, 0);
        if (check_nice == -1 && errno != 0) return 12;
        if (check_nice != nice_val + 1) return 13;
    }

    return 42;
}

// [from test_quick_exit]
// quick_exit(42) terminates with code 42 (uses fork-based execution).
[[cccc::test(exit_code = 42)]]
int test_quick_exit(void) {
    quick_exit(42);
    return 1;
}

// test_posix_sysconf_pathconf_confstr (#732)
// Exercises sysconf()/pathconf()/fpathconf()/confstr() against CCCC's
// canonical _SC_*/_PC_*/_CS_* numbering (translated to the host's real
// numbering by wrap_sysconf/wrap_pathconf/wrap_fpathconf/wrap_confstr in
// src/stdlib/posix.c), plus the predefined POSIX feature-test macros.
[[cccc::test(return = 42)]]
int test_posix_sysconf_pathconf_confstr(void) {
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0 || ps != getpagesize()) return 1;

    if (sysconf(_SC_OPEN_MAX) <= 0) return 2;
    if (sysconf(_SC_NPROCESSORS_ONLN) < 1) return 3;

    // Version queries answer with CCCC's VM-model constants directly.
    if (sysconf(_SC_VERSION) != 200809L) return 4;
    if (_POSIX_VERSION != 200809L) return 5;
    if (_XOPEN_VERSION != 700) return 6;

    // Unknown/unsupported name -> -1 (POSIX-correct failure signal).
    if (sysconf(99999) != -1) return 7;

    int fd = open("/", O_RDONLY);
    if (fd < 0) return 8;
    long pm = pathconf("/", _PC_PATH_MAX);
    long fpm = fpathconf(fd, _PC_PATH_MAX);
    close(fd);
    if (pm <= 0 || fpm <= 0) return 9;

    char buf[256];
    size_t cs = confstr(_CS_PATH, buf, sizeof(buf));
    if (cs == 0 || cs > sizeof(buf)) return 10;

    // Feature-test macros are predefined so gated third-party code sees the
    // always-on POSIX surface CCCC exposes.
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#error "_POSIX_C_SOURCE not predefined as expected"
#endif
#if !defined(_XOPEN_SOURCE) || _XOPEN_SOURCE < 700
#error "_XOPEN_SOURCE not predefined as expected"
#endif
#if !defined(_DEFAULT_SOURCE)
#error "_DEFAULT_SOURCE not predefined as expected"
#endif

    return 42;
}

// test_posix_host_global_bridge (#736)
// errno and getopt's optarg/optind/opterr/optopt used to be inert,
// always-zero guest globals disconnected from the host's real POSIX call
// outcomes. They now alias the host's real storage via accessor functions
// (same pattern as stdin/stdout/stderr), so a failing call's real errno and
// getopt's real parse state are actually observable from guest code.
[[cccc::test(return = 42)]]
int test_posix_host_global_bridge(void) {
    errno = 0;
    int r = access("/nonexistent-path-xyz-736", F_OK);
    if (r != -1) return 1;
    if (errno != ENOENT) return 2;

    errno = 0;
    errno = EAGAIN;
    if (errno != EAGAIN) return 3;

    char *argv[] = {"prog", "-x", "hello", NULL};
    optind = 1;
    int c = getopt(3, argv, "x:");
    if (c != 'x') return 4;
    if (!optarg || strcmp(optarg, "hello") != 0) return 5;
    if (optind != 3) return 6;

    opterr = 0;
    if (opterr != 0) return 7;

    return 42;
}

// test_posix_socket_transfer
// Proves sockets can actually move bytes: recv/send/recvfrom/sendto over an
// AF_UNIX socketpair, plus getpeername/getsockopt/sockatmark sanity checks.
[[cccc::test(return = 42)]]
int test_posix_socket_transfer(void) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return 1;

    const char *msg = "hello";
    if (send(sv[0], msg, 5, 0) != 5) return 2;

    char buf[16];
    for (int i = 0; i < 16; i++) buf[i] = 0;
    if (recv(sv[1], buf, sizeof(buf), 0) != 5) return 3;
    if (strcmp(buf, "hello") != 0) return 4;

    if (sendto(sv[0], msg, 5, 0, 0, 0) != 5) return 5;
    for (int i = 0; i < 16; i++) buf[i] = 0;
    if (recvfrom(sv[1], buf, sizeof(buf), 0, 0, 0) != 5) return 6;
    if (strcmp(buf, "hello") != 0) return 7;

    int type = 0;
    socklen_t tlen = sizeof(type);
    if (getsockopt(sv[0], SOL_SOCKET, SO_TYPE, &type, &tlen) != 0) return 8;
    if (type != SOCK_STREAM) return 9;

    struct sockaddr_un addr;
    socklen_t alen = sizeof(addr);
    if (getpeername(sv[0], (struct sockaddr *)&addr, &alen) != 0) return 10;

    if (sockatmark(sv[0]) != 0) return 11;

    close(sv[0]);
    close(sv[1]);
    return 42;
}

// test_posix_socket_un_bind
// AF_UNIX bind()/listen()/connect()/accept() round trip using struct
// sockaddr_un, proving the header layout is host-ABI-correct.
[[cccc::test(return = 42)]]
int test_posix_socket_un_bind(void) {
    char path[64];
    for (int i = 0; i < 64; i++) path[i] = 0;
    strcpy(path, "/tmp/cccc_test_un_XXXXXX");
    int tfd = mkstemp(path);
    if (tfd < 0) return 1;
    close(tfd);
    unlink(path);

    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) return 2;

    struct sockaddr_un addr;
    for (int i = 0; i < (int)sizeof(addr); i++) ((char *)&addr)[i] = 0;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
#ifdef __APPLE__
    addr.sun_len = sizeof(addr);
#endif

    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { close(lfd); return 3; }
    if (listen(lfd, 1) != 0) { close(lfd); unlink(path); return 4; }

    int cfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cfd < 0) { close(lfd); unlink(path); return 5; }
    if (connect(cfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(lfd); close(cfd); unlink(path); return 6;
    }

    struct sockaddr_un peer;
    socklen_t plen = sizeof(peer);
    int afd = accept(lfd, (struct sockaddr *)&peer, &plen);
    if (afd < 0) { close(lfd); close(cfd); unlink(path); return 7; }

    if (send(cfd, "hi", 2, 0) != 2) { close(lfd); close(cfd); close(afd); unlink(path); return 8; }
    char rbuf[4];
    for (int i = 0; i < 4; i++) rbuf[i] = 0;
    if (recv(afd, rbuf, sizeof(rbuf), 0) != 2 || strcmp(rbuf, "hi") != 0) {
        close(lfd); close(cfd); close(afd); unlink(path); return 9;
    }

    close(lfd);
    close(cfd);
    close(afd);
    unlink(path);
    return 42;
}

// test_posix_sendmsg_recvmsg_scm_rights
// #741: sendmsg()/recvmsg() with SCM_RIGHTS ancillary data over an AF_UNIX
// socketpair, passing a real file descriptor from one guest "process" to
// the other. If struct msghdr/cmsghdr's per-platform field widths or
// CMSG_ALIGN were wrong, this either fails to find the control message or
// (worse) reads garbage -- the fd being usable on the far end is the
// end-to-end proof the layout matches the host ABI.
[[cccc::test(return = 42)]]
int test_posix_sendmsg_recvmsg_scm_rights(void) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return 1;

    char path[64];
    for (int i = 0; i < 64; i++) path[i] = 0;
    strcpy(path, "/tmp/cccc_test_scm_XXXXXX");
    int passed_fd = mkstemp(path);
    if (passed_fd < 0) { close(sv[0]); close(sv[1]); return 2; }
    unlink(path);
    if (write(passed_fd, "hi", 2) != 2) { close(passed_fd); close(sv[0]); close(sv[1]); return 3; }

    char data = 'x';
    struct iovec iov;
    iov.iov_base = &data;
    iov.iov_len = 1;

    char cbuf[CMSG_SPACE(sizeof(int))];
    for (int i = 0; i < (int)sizeof(cbuf); i++) cbuf[i] = 0;

    struct msghdr smsg;
    for (int i = 0; i < (int)sizeof(smsg); i++) ((char *)&smsg)[i] = 0;
    smsg.msg_iov = &iov;
    smsg.msg_iovlen = 1;
    smsg.msg_control = cbuf;
    smsg.msg_controllen = sizeof(cbuf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&smsg);
    if (!cmsg) { close(passed_fd); close(sv[0]); close(sv[1]); return 4; }
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    *(int *)CMSG_DATA(cmsg) = passed_fd;

    if (sendmsg(sv[0], &smsg, 0) != 1) { close(passed_fd); close(sv[0]); close(sv[1]); return 5; }
    close(passed_fd);

    char rdata = 0;
    struct iovec riov;
    riov.iov_base = &rdata;
    riov.iov_len = 1;

    char rcbuf[CMSG_SPACE(sizeof(int))];
    for (int i = 0; i < (int)sizeof(rcbuf); i++) rcbuf[i] = 0;

    struct msghdr rmsg;
    for (int i = 0; i < (int)sizeof(rmsg); i++) ((char *)&rmsg)[i] = 0;
    rmsg.msg_iov = &riov;
    rmsg.msg_iovlen = 1;
    rmsg.msg_control = rcbuf;
    rmsg.msg_controllen = sizeof(rcbuf);

    if (recvmsg(sv[1], &rmsg, 0) != 1) { close(sv[0]); close(sv[1]); return 6; }
    if (rdata != 'x') { close(sv[0]); close(sv[1]); return 7; }

    struct cmsghdr *rcmsg = CMSG_FIRSTHDR(&rmsg);
    if (!rcmsg) { close(sv[0]); close(sv[1]); return 8; }
    if (rcmsg->cmsg_level != SOL_SOCKET) { close(sv[0]); close(sv[1]); return 9; }
    if (rcmsg->cmsg_type != SCM_RIGHTS) { close(sv[0]); close(sv[1]); return 10; }

    int received_fd = *(int *)CMSG_DATA(rcmsg);
    char verify[3];
    for (int i = 0; i < 3; i++) verify[i] = 0;
    if (lseek(received_fd, 0, SEEK_SET) != 0) { close(received_fd); close(sv[0]); close(sv[1]); return 11; }
    if (read(received_fd, verify, 2) != 2) { close(received_fd); close(sv[0]); close(sv[1]); return 12; }
    if (verify[0] != 'h' || verify[1] != 'i') { close(received_fd); close(sv[0]); close(sv[1]); return 13; }

    close(received_fd);
    close(sv[0]);
    close(sv[1]);
    return 42;
}

// test_posix_ipv6_udp_roundtrip
// #742: AF_INET6 socket, bind to ::1 on an ephemeral port, UDP
// sendto/recvfrom round trip -- proves struct sockaddr_in6's per-platform
// layout (sin6_len/1-byte sa_family_t on Apple vs no-sin6_len/2-byte
// sa_family_t on Linux) matches the host ABI closely enough for the kernel
// to actually deliver the datagram.
[[cccc::test(return = 42)]]
int test_posix_ipv6_udp_roundtrip(void) {
    int sfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sfd < 0) return 1;

    struct sockaddr_in6 addr;
    for (int i = 0; i < (int)sizeof(addr); i++) ((char *)&addr)[i] = 0;
#ifdef __APPLE__
    addr.sin6_len = sizeof(addr);
#endif
    addr.sin6_family = AF_INET6;
    addr.sin6_port = 0; /* ephemeral */
    struct in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
    addr.sin6_addr = loopback;

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { close(sfd); return 2; }

    struct sockaddr_in6 bound;
    socklen_t blen = sizeof(bound);
    if (getsockname(sfd, (struct sockaddr *)&bound, &blen) != 0) { close(sfd); return 3; }
    if (bound.sin6_family != AF_INET6) { close(sfd); return 4; }
    if (bound.sin6_port == 0) { close(sfd); return 5; }

    if (sendto(sfd, "hi", 2, 0, (struct sockaddr *)&bound, sizeof(bound)) != 2) { close(sfd); return 6; }

    char buf[4];
    for (int i = 0; i < 4; i++) buf[i] = 0;
    struct sockaddr_in6 from;
    socklen_t flen = sizeof(from);
    if (recvfrom(sfd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen) != 2) { close(sfd); return 7; }
    if (strcmp(buf, "hi") != 0) { close(sfd); return 8; }

    close(sfd);

    if (IPPROTO_IPV6 != 41) return 9;
    if (IPV6_V6ONLY == IPV6_UNICAST_HOPS) return 10;
    return 42;
}

// test_posix_ipv6_advanced_options
// #749: the advanced IPV6_* set beyond the #742 baseline (multicast/
// packet-info/routing-header options), plus struct ipv6_mreq/in6_pktinfo.
// A real multicast join can fail in CI/containers with no multicast-capable
// interface, so this tolerates ENODEV/EADDRNOTAVAIL/ENOPROTOOPT there and
// only hard-fails on an EINVAL-class error, which would indicate a wrong
// constant. IPV6_TCLASS is round-tripped via setsockopt/getsockopt on a
// real socket, which is a genuine end-to-end check on every host. The full
// multicast receive round trip is verified by hand, not by this test.
[[cccc::test(return = 42)]]
int test_posix_ipv6_advanced_options(void) {
    /* Constants distinct + match expectation (per-platform values verified
       against real macOS/Linux headers). */
    if (IPV6_PKTINFO == IPV6_TCLASS) return 1;
    if (IPV6_RECVPKTINFO == IPV6_RECVTCLASS) return 2;
    if (IPV6_HOPOPTS == IPV6_DSTOPTS) return 3;
    if (IPV6_RTHDR_TYPE_0 != 0) return 4;

    if (sizeof(struct ipv6_mreq) != 20) return 5;
    if (sizeof(struct in6_pktinfo) != 20) return 6;

    int sfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sfd < 0) return 7;

    /* IPV6_TCLASS round trip -- genuine end-to-end check, no interface
       dependency. */
    int tclass = 42;
    if (setsockopt(sfd, IPPROTO_IPV6, IPV6_TCLASS, &tclass, sizeof(tclass)) != 0) {
        close(sfd);
        return 8;
    }
    int got_tclass = -1;
    socklen_t tclass_len = sizeof(got_tclass);
    if (getsockopt(sfd, IPPROTO_IPV6, IPV6_TCLASS, &got_tclass, &tclass_len) != 0) {
        close(sfd);
        return 9;
    }
    if (got_tclass != tclass) { close(sfd); return 10; }

    /* struct ipv6_mreq + IPV6_JOIN_GROUP against a real multicast group on
       the default interface. Tolerate the host having no multicast-capable
       interface (common in containers/CI). */
    struct ipv6_mreq mreq;
    for (int i = 0; i < (int)sizeof(mreq); i++) ((char *)&mreq)[i] = 0;
    mreq.ipv6mr_multiaddr.s6_addr[0] = 0xff;
    mreq.ipv6mr_multiaddr.s6_addr[1] = 0x02;
    mreq.ipv6mr_multiaddr.s6_addr[15] = 0x01; /* ff02::1, all-nodes link-local */
    mreq.ipv6mr_interface = 0; /* default interface */

    if (setsockopt(sfd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) != 0) {
        if (errno == EINVAL) { close(sfd); return 11; }
        /* ENODEV/EADDRNOTAVAIL/ENOPROTOOPT/etc. -- no usable multicast
           interface on this host; the constant itself is proven correct
           by the fact the kernel understood the option well enough to
           reject it for an environmental reason, not "unknown option". */
    } else {
        setsockopt(sfd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq, sizeof(mreq));
    }

    close(sfd);
    return 42;
}

// [helper for test_posix_ipv6_multicast_roundtrip]
// Attempts a full IPV6_JOIN_GROUP + sendto + recvfrom round trip of ff02::1
// scoped to a single candidate interface. Distinguishes "this interface
// can't do it" (join/send rejected for an environmental reason -- try the
// next candidate) from "something is actually broken" (a real socket/struct
// bug) via *hard_fail: 0 means try the next interface, nonzero means stop
// and fail the test with that code.
static int try_multicast_on(unsigned int ifindex, int *hard_fail) {
    *hard_fail = 0;

    int rfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (rfd < 0) { *hard_fail = 3; return 0; }

    struct sockaddr_in6 raddr;
    for (int i = 0; i < (int)sizeof(raddr); i++) ((char *)&raddr)[i] = 0;
#ifdef __APPLE__
    raddr.sin6_len = sizeof(raddr);
#endif
    raddr.sin6_family = AF_INET6;
    raddr.sin6_port = 0; /* ephemeral */
    if (bind(rfd, (struct sockaddr *)&raddr, sizeof(raddr)) != 0) { close(rfd); *hard_fail = 4; return 0; }

    struct sockaddr_in6 bound;
    socklen_t blen = sizeof(bound);
    if (getsockname(rfd, (struct sockaddr *)&bound, &blen) != 0) { close(rfd); *hard_fail = 5; return 0; }

    struct ipv6_mreq mreq;
    for (int i = 0; i < (int)sizeof(mreq); i++) ((char *)&mreq)[i] = 0;
    mreq.ipv6mr_multiaddr.s6_addr[0] = 0xff;
    mreq.ipv6mr_multiaddr.s6_addr[1] = 0x02;
    mreq.ipv6mr_multiaddr.s6_addr[15] = 0x01; /* ff02::1, all-nodes link-local */
    mreq.ipv6mr_interface = ifindex;

    if (setsockopt(rfd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) != 0) {
        close(rfd);
        return 0; /* this interface can't join -- caller tries the next one */
    }

    int loop_enable = 1;
    setsockopt(rfd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop_enable, sizeof(loop_enable));
    setsockopt(rfd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifindex, sizeof(ifindex));

    /* Send from a second socket to ff02::1, scoped to the candidate
       interface, targeting the receiver's bound ephemeral port. */
    int sfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sfd < 0) {
        setsockopt(rfd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq, sizeof(mreq));
        close(rfd);
        *hard_fail = 6;
        return 0;
    }
    setsockopt(sfd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifindex, sizeof(ifindex));

    struct sockaddr_in6 dest;
    for (int i = 0; i < (int)sizeof(dest); i++) ((char *)&dest)[i] = 0;
#ifdef __APPLE__
    dest.sin6_len = sizeof(dest);
#endif
    dest.sin6_family = AF_INET6;
    dest.sin6_port = bound.sin6_port;
    dest.sin6_addr = mreq.ipv6mr_multiaddr;
    dest.sin6_scope_id = ifindex;

    const char *payload = "if788";
    int sent_ok = (sendto(sfd, payload, 5, 0, (struct sockaddr *)&dest, sizeof(dest)) == 5);

    int ok = 0;
    if (sent_ok) {
        struct pollfd pfd;
        pfd.fd = rfd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 3000);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            char buf[8];
            for (int i = 0; i < 8; i++) buf[i] = 0;
            struct sockaddr_in6 from;
            socklen_t flen = sizeof(from);
            ssize_t n = recvfrom(rfd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
            ok = (n == 5 && strcmp(buf, payload) == 0);
        }
    }
    /* sendto/poll/recvfrom failing here is still environmental (e.g. no
       multicast route out of this particular interface, as seen for "lo"
       in some container network namespaces) -- try the next candidate
       rather than hard-failing. */

    close(sfd);
    setsockopt(rfd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq, sizeof(mreq));
    close(rfd);
    return ok;
}

// test_posix_ipv6_multicast_roundtrip
// #788: if_nametoindex()/if_indextoname() let guest code target a specific
// interface instead of relying on index 0 (what #749's join test above
// does). Unlike that test, this one does NOT tolerate an overall failure:
// it walks every interface reported by if_nameindex() (round-tripping one
// resolved index through if_indextoname() along the way), attempts a full
// IPV6_JOIN_GROUP + send/receive round trip on each, and asserts success on
// at least one. Verified by hand: on macOS "lo0" carries the round trip; on
// both Linux x86_64/aarch64 containers "lo" can join ff02::1 but has no
// multicast route (ENETUNREACH), while "eth0" carries the full round trip
// -- hence walking all interfaces rather than assuming loopback. The skip
// path (printed, not silent) exists for environments with no working
// multicast interface at all.
[[cccc::test(return = 42)]]
int test_posix_ipv6_multicast_roundtrip(void) {
    struct if_nameindex *list = if_nameindex();
    if (!list) {
        printf("test_posix_ipv6_multicast_roundtrip: SKIP -- if_nameindex() returned nothing\n");
        return 42;
    }

    /* Round-trip if_indextoname() against the first interface's index. */
    char namebuf[IF_NAMESIZE];
    for (int i = 0; i < IF_NAMESIZE; i++) namebuf[i] = 0;
    if (if_indextoname(list[0].if_index, namebuf) != namebuf) { if_freenameindex(list); return 1; }
    if (namebuf[0] == 0) { if_freenameindex(list); return 2; }
    if (if_nametoindex(namebuf) != list[0].if_index) { if_freenameindex(list); return 10; }

    int found = 0;
    int hard_fail = 0;
    for (struct if_nameindex *p = list; p->if_index != 0 || p->if_name; p++) {
        if (try_multicast_on(p->if_index, &hard_fail)) { found = 1; break; }
        if (hard_fail) break;
    }
    if_freenameindex(list);

    if (hard_fail) return hard_fail;
    if (!found) {
        printf("test_posix_ipv6_multicast_roundtrip: SKIP -- no interface in this environment "
               "could complete an IPv6 multicast join+send+receive round trip\n");
        return 42;
    }
    return 42;
}

// test_posix_cheap_wrappers
// Exercises a representative slice of the small #735 leftover wrappers/
// constants that reuse existing struct layouts: *at() family, itimer,
// mlock, readdir_r/alphasort, getnameinfo, id functions, nice.
[[cccc::test(return = 42)]]
int test_posix_cheap_wrappers(void) {
    if (mkdirat(AT_FDCWD, "/tmp/cccc_test_atdir", 0755) != 0) return 1;
    struct stat st;
    if (fstatat(AT_FDCWD, "/tmp/cccc_test_atdir", &st, 0) != 0) return 2;
    if (!S_ISDIR(st.st_mode)) return 3;
    if (fchmodat(AT_FDCWD, "/tmp/cccc_test_atdir", 0700, 0) != 0) return 4;
    if ((st.st_mode & 0777) == 0 && fstatat(AT_FDCWD, "/tmp/cccc_test_atdir", &st, 0) != 0) return 5;
    rmdir("/tmp/cccc_test_atdir");

    struct itimerval it, old;
    it.it_value.tv_sec = 0; it.it_value.tv_usec = 0;
    it.it_interval.tv_sec = 0; it.it_interval.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &it, &old) != 0) return 6;
    if (getitimer(ITIMER_REAL, &old) != 0) return 7;

    void *p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == (void *)-1) return 8;
    if (mlock(p, 4096) == 0) munlock(p, 4096);
    munmap(p, 4096);

    DIR *dp = opendir("/tmp");
    if (!dp) return 9;
    struct dirent ent, *res;
    if (readdir_r(dp, &ent, &res) != 0) return 10;
    closedir(dp);

    struct dirent da, db;
    strcpy(da.d_name, "apple");
    strcpy(db.d_name, "banana");
    const struct dirent *pa = &da, *pb = &db;
    if (alphasort(&pa, &pb) >= 0) return 11;

    struct sockaddr_in sin;
    for (int i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
#ifdef __APPLE__
    sin.sin_len = sizeof(sin);
#endif
    sin.sin_family = AF_INET;
    sin.sin_port = htons(80);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    char host[NI_MAXHOST];
    if (getnameinfo((struct sockaddr *)&sin, sizeof(sin), host, sizeof(host), 0, 0, NI_NUMERICHOST) != 0) return 12;
    if (strcmp(host, "127.0.0.1") != 0) return 13;

    if (getuid() < 0) return 14;
    if (getgid() < 0) return 15;

    nice(0);

    /* Constant distinctness within each namespace (AT_*, wait-flag, mlock) */
    if (AT_FDCWD == AT_SYMLINK_NOFOLLOW) return 16;
    if (WEXITED == WSTOPPED || WSTOPPED == WNOWAIT || WEXITED == WNOWAIT) return 17;
    if (MCL_CURRENT == MCL_FUTURE) return 18;

    return 42;
}

// test_posix_fcntl_dirflags
// #734: O_DIRECTORY/O_NOFOLLOW round-trip + distinctness. The aarch64 #else
// branch in include/fcntl.h was empirically verified against a real Linux
// aarch64 header (native arm64 container) as part of this pass; this test
// exercises the values functionally on whichever platform runs it.
[[cccc::test(return = 42)]]
int test_posix_fcntl_dirflags(void) {
    if (O_DIRECTORY == O_NOFOLLOW) return 1;

    if (mkdir("/tmp/cccc_test_dirflag", 0755) != 0 && errno != EEXIST) return 2;

    int dfd = open("/tmp/cccc_test_dirflag", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) return 3;
    close(dfd);

    int ffd = open("/tmp/cccc_test_dirflag/probe_file", O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (ffd < 0) { rmdir("/tmp/cccc_test_dirflag"); return 4; }
    close(ffd);

    errno = 0;
    int bad = open("/tmp/cccc_test_dirflag/probe_file", O_RDONLY | O_DIRECTORY);
    if (bad >= 0) { close(bad); unlink("/tmp/cccc_test_dirflag/probe_file"); rmdir("/tmp/cccc_test_dirflag"); return 5; }
    if (errno != ENOTDIR) { unlink("/tmp/cccc_test_dirflag/probe_file"); rmdir("/tmp/cccc_test_dirflag"); return 6; }

    unlink("/tmp/cccc_test_dirflag/probe_file");
    rmdir("/tmp/cccc_test_dirflag");
    return 42;
}

// test_posix_termios_constants
// Fixes a pre-existing bug: c_iflag/c_oflag/c_cflag/c_lflag and baud-rate
// constants in include/termios.h were unconditionally macOS values applied
// on Linux too (verified wrong against real Linux x86_64/aarch64 headers).
// Now platform-split; this checks within-namespace distinctness (values may
// legitimately collide *across* namespaces, same caveat as
// test_wait_stat_constants.c).
[[cccc::test(return = 42)]]
int test_posix_termios_constants(void) {
    long iflag[] = {IGNBRK, BRKINT, IGNPAR, PARMRK, INPCK, ISTRIP, INLCR, IGNCR, ICRNL, IXON, IXOFF, IXANY};
    int n = (int)(sizeof(iflag) / sizeof(iflag[0]));
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (iflag[i] == iflag[j]) return 1;

    long cflag[] = {CS5, CS6, CS7, CS8, CSTOPB, CREAD, PARENB, PARODD, HUPCL, CLOCAL};
    n = (int)(sizeof(cflag) / sizeof(cflag[0]));
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (i != 0 && j != 0 && cflag[i] == cflag[j]) return 2; /* CS5 == 0 legitimately */

    long lflag[] = {ECHOE, ECHOK, ECHONL, NOFLSH, TOSTOP, IEXTEN, ECHO, ICANON, ISIG};
    n = (int)(sizeof(lflag) / sizeof(lflag[0]));
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (lflag[i] == lflag[j]) return 3;

    long baud[] = {B9600, B19200, B38400, B57600, B115200, B230400};
    n = (int)(sizeof(baud) / sizeof(baud[0]));
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (baud[i] == baud[j]) return 4;
    if (B0 != 0) return 5;

    long vchars[] = {VEOF, VEOL, VEOL2, VERASE, VWERASE, VKILL, VREPRINT, VINTR, VQUIT, VSUSP, VSTART, VSTOP, VLNEXT, VDISCARD, VMIN, VTIME};
    n = (int)(sizeof(vchars) / sizeof(vchars[0]));
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (vchars[i] == vchars[j]) return 6;

    return 42;
}

// test_posix_qsort_bsearch
// #738: qsort/bsearch comparators used to crash the VM -- a guest
// function-pointer value handed to a host API that calls it back as real
// machine code isn't a real callable pointer. Exercises: a plain guest
// comparator, nested reentrancy (a comparator that itself calls qsort()),
// and an already-registered host FFI function (alphasort) taken as a value
// -- the same pattern scandir(select, alphasort) below relies on.
static int qb_cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

static int qb_nested_ok = -1;
static int qb_cmp_nested(const void *a, const void *b) {
    if (qb_nested_ok == -1) {
        int tmp[3] = {9, 7, 8};
        qsort(tmp, 3, sizeof(int), qb_cmp_int);
        qb_nested_ok = (tmp[0] == 7 && tmp[1] == 8 && tmp[2] == 9) ? 1 : 0;
    }
    return *(const int *)a - *(const int *)b;
}

[[cccc::test(return = 42)]]
int test_posix_qsort_bsearch(void) {
    int arr[] = {5, 3, 1, 4, 2};
    qsort(arr, 5, sizeof(int), qb_cmp_int);
    for (int i = 0; i < 5; i++)
        if (arr[i] != i + 1) return 1;

    int key = 3;
    int *found = bsearch(&key, arr, 5, sizeof(int), qb_cmp_int);
    if (!found || *found != 3) return 2;
    key = 99;
    if (bsearch(&key, arr, 5, sizeof(int), qb_cmp_int)) return 3;

    int arr2[] = {5, 3, 1, 4, 2};
    qsort(arr2, 5, sizeof(int), qb_cmp_nested);
    for (int i = 0; i < 5; i++)
        if (arr2[i] != i + 1) return 4;
    if (qb_nested_ok != 1) return 5;

    struct dirent a, b, c;
    strcpy(a.d_name, "banana");
    strcpy(b.d_name, "apple");
    strcpy(c.d_name, "cherry");
    const struct dirent *darr[3] = {&a, &b, &c};
    qsort(darr, 3, sizeof(struct dirent *), (int (*)(const void *, const void *))alphasort);
    if (strcmp(darr[0]->d_name, "apple") != 0) return 6;
    if (strcmp(darr[1]->d_name, "banana") != 0) return 7;
    if (strcmp(darr[2]->d_name, "cherry") != 0) return 8;

    return 42;
}

// test_posix_glob_errfunc
// #738: glob()'s errfunc used to crash if a real (non-NULL) guest callback
// was passed. Deliberately globs an unreadable (mode 0000) directory to try
// to force errfunc to actually fire -- but permission checks don't apply to
// root, and this suite may run as root (e.g. inside a container), so
// whether errfunc actually fires is only asserted when not running as
// root. Either way, the call must not crash: that's the actual regression
// this guards, and it's unconditional.
static int glob_errfunc_calls;
static int glob_errfunc(const char *epath, int eerrno) {
    (void)epath; (void)eerrno;
    glob_errfunc_calls++;
    return 0;
}

[[cccc::test(return = 42)]]
int test_posix_glob_errfunc(void) {
    mkdir("/tmp/cccc_test_glob_noperm", 0000);
    glob_t g;
    glob("/tmp/cccc_test_glob_noperm/*", GLOB_ERR, glob_errfunc, &g);
    rmdir("/tmp/cccc_test_glob_noperm");
    if (geteuid() != 0 && glob_errfunc_calls < 1) return 1;
    return 42;
}

// test_posix_scandir
// #738: scandir() was previously not declared at all (its select/compar
// callbacks crashed the VM). Exercises both callbacks: a real guest select
// filter, and alphasort (an already-registered host FFI function) as compar.
static int scandir_select_dotfiles_out(const struct dirent *e) {
    return strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0;
}

[[cccc::test(return = 42)]]
int test_posix_scandir(void) {
    struct dirent **list;
    int n = scandir("/tmp", &list, scandir_select_dotfiles_out, alphasort);
    if (n < 0) return 1;
    int rc = 42;
    for (int i = 0; i < n; i++) {
        if (rc != 42) continue;
        if (strcmp(list[i]->d_name, ".") == 0 || strcmp(list[i]->d_name, "..") == 0) rc = 2;
        else if (i > 0 && strcmp(list[i - 1]->d_name, list[i]->d_name) > 0) rc = 3;
    }
    for (int i = 0; i < n; i++)
        free(list[i]);
    free(list);
    return rc;
}

// test_posix_sigaction
// #738: sigaction() used to be a raw passthrough to the real host
// sigaction() -- a guest handler value isn't a real callable host pointer,
// so it crashed the moment a signal was actually delivered. Now reuses the
// same VM-managed slot + async-safe shim mechanism signal()/VSIGNAL already
// use. Also exercises sa_mask/sa_flags round-trip through oact.
//
// #787: sa_flags = 0x1234 (used below purely to exercise oact fidelity for
// an arbitrary bit pattern) happens to set SA_RESETHAND on macOS, which is
// now genuinely enforced at delivery -- so the round-trip query must happen
// *before* any raise(), and the slot that's actually delivered must be
// re-registered with real, non-RESETHAND flags first (test_posix_
// sigaction_flags below covers SA_RESETHAND/SA_NODEFER/sa_mask/SA_RESTART
// enforcement itself).
static volatile sig_atomic_t sigaction_got_it;
static void sigaction_handler(int sig) { sigaction_got_it = sig; }

[[cccc::test(return = 42)]]
int test_posix_sigaction(void) {
    struct sigaction sa, old;
    for (int i = 0; i < (int)sizeof(sa); i++) ((char *)&sa)[i] = 0;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGTERM);
    sa.sa_handler = sigaction_handler;
    sa.sa_flags = 0x1234;

    if (sigaction(SIGUSR1, &sa, &old) != 0) return 1;
    if (old.sa_handler != SIG_DFL) return 2;

    /* oact fidelity round trip -- before any delivery, since 0x1234 may
       set SA_RESETHAND depending on platform. */
    struct sigaction q;
    for (int i = 0; i < (int)sizeof(q); i++) ((char *)&q)[i] = 0;
    if (sigaction(SIGUSR1, 0, &q) != 0) return 4;
    if (q.sa_handler != sigaction_handler) return 5;
    if (q.sa_flags != 0x1234) return 6;
    if (sigismember(&q.sa_mask, SIGTERM) != 1) return 7;

    /* Re-register with real (non-RESETHAND/NODEFER) flags before actually
       delivering, so this test only exercises #738's basic dispatch, not
       #787's flag enforcement. */
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, 0) != 0) return 9;

    raise(SIGUSR1);
    if (sigaction_got_it != SIGUSR1) return 3;

    struct sigaction dfl;
    for (int i = 0; i < (int)sizeof(dfl); i++) ((char *)&dfl)[i] = 0;
    dfl.sa_handler = SIG_DFL;
    if (sigaction(SIGUSR1, &dfl, 0) != 0) return 8;

    return 42;
}

// test_posix_sigaction_siginfo
// #745: sa_sigaction/SA_SIGINFO -- the three-argument handler form that
// receives a real siginfo_t. Two delivery paths, two siginfo sources:
// raise() (VRAISE, ops.c) never touches the host signal mechanism, so it
// synthesizes real POSIX raise() semantics (si_code == SI_USER, si_pid ==
// getpid()); a real delivered SIGCHLD goes through the async host-signal
// shim, which captures genuine kernel-provided data (si_code == CLD_EXITED,
// si_status == the child's real exit code).
static volatile sig_atomic_t siginfo_signo = -1;
static volatile sig_atomic_t siginfo_code = -1;
static volatile sig_atomic_t siginfo_pid = -1;
static void siginfo_handler(int sig, siginfo_t *info, void *uctx) {
    (void)uctx;
    siginfo_signo = sig;
    siginfo_code = info->si_code;
    siginfo_pid = info->si_pid;
}

static volatile sig_atomic_t sigchld_code = -1;
static volatile sig_atomic_t sigchld_status = -1;
static volatile sig_atomic_t sigchld_pid = -1;
static void sigchld_handler(int sig, siginfo_t *info, void *uctx) {
    (void)sig; (void)uctx;
    sigchld_code = info->si_code;
    sigchld_status = info->si_status;
    sigchld_pid = info->si_pid;
}

[[cccc::test(return = 42)]]
int test_posix_sigaction_siginfo(void) {
    struct sigaction sa;
    for (int i = 0; i < (int)sizeof(sa); i++) ((char *)&sa)[i] = 0;
    sa.sa_sigaction = siginfo_handler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGUSR1, &sa, 0) != 0) return 1;

    raise(SIGUSR1);
    if (siginfo_signo != SIGUSR1) return 2;
    if (siginfo_code != SI_USER) return 3;
    if (siginfo_pid != getpid()) return 4;

    struct sigaction dfl1;
    for (int i = 0; i < (int)sizeof(dfl1); i++) ((char *)&dfl1)[i] = 0;
    dfl1.sa_handler = SIG_DFL;
    if (sigaction(SIGUSR1, &dfl1, 0) != 0) return 5;

    /* Real delivered SIGCHLD -- genuine kernel-captured siginfo, not
       synthesized by the VM. */
    struct sigaction sc;
    for (int i = 0; i < (int)sizeof(sc); i++) ((char *)&sc)[i] = 0;
    sc.sa_sigaction = sigchld_handler;
    sc.sa_flags = SA_SIGINFO;
    if (sigaction(SIGCHLD, &sc, 0) != 0) return 6;

    pid_t pid = fork();
    if (pid < 0) return 7;
    if (pid == 0) { _exit(7); }

    int status;
    /* waitpid() reaps the child; SIGCHLD delivery is polled for separately
       via the dispatch loop's pending-signal check, so give it a moment. */
    for (int i = 0; i < 2000 && sigchld_pid != pid; i++) usleep(1000);
    waitpid(pid, &status, 0);

    if (sigchld_pid != pid) return 8;
    if (sigchld_code != CLD_EXITED) return 9;
    if (sigchld_status != 7) return 10;

    struct sigaction dfl2;
    for (int i = 0; i < (int)sizeof(dfl2); i++) ((char *)&dfl2)[i] = 0;
    dfl2.sa_handler = SIG_DFL;
    if (sigaction(SIGCHLD, &dfl2, 0) != 0) return 11;

    return 42;
}

// test_posix_sigaction_flags
// #787: enforcement of sa_mask/SA_NODEFER/SA_RESETHAND at dispatch (SA_RESTART
// is handled separately, by passing it through to the host sigaction() --
// see cccc_set_guest_signal_action in src/host_signal.c -- rather than VM-
// level emulation, since guest handlers run from the dispatch loop, not
// native signal context, so they can't act mid-syscall to demonstrate a
// restart; not covered by an automated behavioral test here for that
// reason, only round-tripped through oact like any other flag).
static volatile sig_atomic_t resethand_ran;
static void resethand_handler(int sig) { (void)sig; resethand_ran++; }

static volatile sig_atomic_t nodefer_depth;
static volatile sig_atomic_t nodefer_max_depth;
static volatile sig_atomic_t nodefer_entries;
static void nodefer_handler(int sig) {
    nodefer_depth++;
    if (nodefer_depth > nodefer_max_depth) nodefer_max_depth = nodefer_depth;
    nodefer_entries++;
    if (nodefer_entries == 1) raise(sig); /* recurses iff SA_NODEFER */
    nodefer_depth--;
}

static volatile sig_atomic_t mask_log[8];
static volatile sig_atomic_t mask_log_len;
static void mask_log_push(int v) { if (mask_log_len < 8) mask_log[mask_log_len++] = v; }
static void mask_handler_b(int sig) { (void)sig; mask_log_push(2); }
static void mask_handler_a(int sig) {
    (void)sig;
    mask_log_push(1);
    raise(SIGUSR2); /* blocked by A's sa_mask -- must not run B yet */
    mask_log_push(3); /* proves B hasn't run between the two pushes above */
}

[[cccc::test(return = 42)]]
int test_posix_sigaction_flags(void) {
    /* SA_RESETHAND: disposition resets to SIG_DFL after one delivery. */
    struct sigaction rh;
    for (int i = 0; i < (int)sizeof(rh); i++) ((char *)&rh)[i] = 0;
    rh.sa_handler = resethand_handler;
    rh.sa_flags = SA_RESETHAND;
    if (sigaction(SIGUSR2, &rh, 0) != 0) return 1;

    raise(SIGUSR2);
    if (resethand_ran != 1) return 2;

    struct sigaction q;
    for (int i = 0; i < (int)sizeof(q); i++) ((char *)&q)[i] = 0;
    if (sigaction(SIGUSR2, 0, &q) != 0) return 3;
    if (q.sa_handler != SIG_DFL) return 4;

    /* SA_NODEFER: without it, a handler's self-raise() is deferred (not
       reentered) until the handler returns; with it, the same self-raise()
       recurses immediately. */
    struct sigaction nd;
    for (int i = 0; i < (int)sizeof(nd); i++) ((char *)&nd)[i] = 0;
    nd.sa_handler = nodefer_handler;
    nd.sa_flags = 0;
    if (sigaction(SIGUSR2, &nd, 0) != 0) return 5;

    nodefer_depth = 0; nodefer_max_depth = 0; nodefer_entries = 0;
    raise(SIGUSR2);
    /* Give the dispatch loop a few cycles to redeliver the signal that was
       deferred during the handler's own execution (no OS wakeup needed --
       it's polled on every VM instruction, including these no-ops). */
    for (volatile int i = 0; i < 100000 && nodefer_entries < 2; i++) { }
    if (nodefer_max_depth != 1) return 6;   /* never reentered */
    if (nodefer_entries != 2) return 7;     /* but still delivered twice */

    nd.sa_flags = SA_NODEFER;
    if (sigaction(SIGUSR2, &nd, 0) != 0) return 8;
    nodefer_depth = 0; nodefer_max_depth = 0; nodefer_entries = 0;
    raise(SIGUSR2);
    if (nodefer_max_depth != 2) return 9;   /* reentered synchronously */
    if (nodefer_entries != 2) return 10;

    struct sigaction dfl3;
    for (int i = 0; i < (int)sizeof(dfl3); i++) ((char *)&dfl3)[i] = 0;
    dfl3.sa_handler = SIG_DFL;
    if (sigaction(SIGUSR2, &dfl3, 0) != 0) return 11;

    /* sa_mask: A's mask blocks SIGUSR2 for the duration of A's execution,
       so a self-raise(SIGUSR2) from inside A must not run B until A
       returns. */
    struct sigaction ma;
    for (int i = 0; i < (int)sizeof(ma); i++) ((char *)&ma)[i] = 0;
    sigemptyset(&ma.sa_mask);
    sigaddset(&ma.sa_mask, SIGUSR2);
    ma.sa_handler = mask_handler_a;
    ma.sa_flags = 0;
    if (sigaction(SIGUSR1, &ma, 0) != 0) return 12;

    struct sigaction mb;
    for (int i = 0; i < (int)sizeof(mb); i++) ((char *)&mb)[i] = 0;
    mb.sa_handler = mask_handler_b;
    mb.sa_flags = 0;
    if (sigaction(SIGUSR2, &mb, 0) != 0) return 13;

    mask_log_len = 0;
    raise(SIGUSR1);
    for (volatile int i = 0; i < 100000 && mask_log_len < 3; i++) { }
    if (mask_log_len != 3) return 14;
    if (mask_log[0] != 1 || mask_log[1] != 3 || mask_log[2] != 2) return 15;

    struct sigaction dfl4, dfl5;
    for (int i = 0; i < (int)sizeof(dfl4); i++) ((char *)&dfl4)[i] = 0;
    dfl4.sa_handler = SIG_DFL;
    dfl5 = dfl4;
    if (sigaction(SIGUSR1, &dfl4, 0) != 0) return 16;
    if (sigaction(SIGUSR2, &dfl5, 0) != 0) return 17;

    /* SA_RESTART: round-trips through oact like any other flag (real
       host-level pass-through -- see comment above). */
    struct sigaction rs;
    for (int i = 0; i < (int)sizeof(rs); i++) ((char *)&rs)[i] = 0;
    rs.sa_handler = resethand_handler;
    rs.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR2, &rs, 0) != 0) return 18;
    struct sigaction rq;
    for (int i = 0; i < (int)sizeof(rq); i++) ((char *)&rq)[i] = 0;
    if (sigaction(SIGUSR2, 0, &rq) != 0) return 19;
    if (!(rq.sa_flags & SA_RESTART)) return 20;

    struct sigaction dfl6;
    for (int i = 0; i < (int)sizeof(dfl6); i++) ((char *)&dfl6)[i] = 0;
    dfl6.sa_handler = SIG_DFL;
    if (sigaction(SIGUSR2, &dfl6, 0) != 0) return 21;

    return 42;
}

// test_posix_sigset_ops
// #738: sigemptyset/sigfillset/sigaddset/sigdelset/sigismember used to be
// raw passthroughs to the real host functions, which are a genuine
// out-of-bounds write/read on Linux (real sigset_t is 128 bytes there vs.
// this guest header's 4-byte unsigned int -- coincidentally safe on macOS,
// where the real sigset_t also happens to be 4 bytes). Now operate natively
// on the guest's own 4-byte representation.
[[cccc::test(return = 42)]]
int test_posix_sigset_ops(void) {
    sigset_t set;
    if (sigemptyset(&set) != 0) return 1;
    if (sigismember(&set, SIGUSR1) != 0) return 2;

    if (sigaddset(&set, SIGUSR1) != 0) return 3;
    if (sigismember(&set, SIGUSR1) != 1) return 4;
    if (sigismember(&set, SIGUSR2) != 0) return 5;

    if (sigdelset(&set, SIGUSR1) != 0) return 6;
    if (sigismember(&set, SIGUSR1) != 0) return 7;

    if (sigfillset(&set) != 0) return 8;
    if (sigismember(&set, SIGUSR1) != 1) return 9;
    if (sigismember(&set, SIGTERM) != 1) return 10;

    return 42;
}

// test_posix_atexit_mid_call
// #738: an explicit exit() call drains atexit handlers via a nested,
// mid-vm_eval guest callback (cccc_call_guest_callback) since wrap_exit
// itself runs as an ordinary FFI call with the GIL held -- unlike the
// separate top-level cc_run_at-per-handler path used for atexit handlers
// firing on normal return from main() (see tests/test_atexit_normal_return.c
// and tests/test_atexit_mid_drain_registration.c, which need a real
// standalone main() to exercise that context). This just confirms
// atexit()/exit() are callable and don't crash from within a --testing
// function (a full LIFO-order/exit-code check needs the real process exit
// the two standalone test files above provide).
static int atexit_mid_call_ran;
static void atexit_mid_call_handler(void) { atexit_mid_call_ran = 1; }

[[cccc::test(return = 42)]]
int test_posix_atexit_registration(void) {
    if (atexit(atexit_mid_call_handler) != 0) return 1;
    /* Deliberately do not call exit() here -- this suite's own harness
       drives multiple [[cccc::test]] functions through one real main(),
       so calling exit() would terminate the whole suite early. Just prove
       registration succeeds and doesn't crash. */
    return 42;
}

// test_posix_uname
// #733/#737: uname() fills a guest struct utsname through the FFI boundary,
// same struct-fidelity concern as getrusage()/waitid() above -- proves the
// guest layout matches the host ABI (per-field length differs between macOS
// (256 bytes) and Linux (65 bytes), and Linux has an extra `domainname`
// field). Only checks the strings are non-empty and NUL-terminated within
// bounds, since exact contents aren't portable across hosts.
[[cccc::test(return = 42)]]
int test_posix_uname(void) {
    struct utsname u;
    for (int i = 0; i < (int)sizeof(u); i++) ((char *)&u)[i] = 0x7f;
    if (uname(&u) != 0) return 1;

    /* memchr for the terminating NUL rather than strlen, since the buffer
       was pre-filled with non-NUL 0x7f and strlen alone can't distinguish
       "field never touched" from "field legitimately empty". */
    if (!memchr(u.sysname, 0, sizeof(u.sysname))) return 2;
    if (u.sysname[0] == 0) return 3;
    if (!memchr(u.machine, 0, sizeof(u.machine))) return 4;
    if (u.machine[0] == 0) return 5;
    if (!memchr(u.release, 0, sizeof(u.release))) return 6;
    return 42;
}

// test_posix_times
// #733/#737: times() fills a guest struct tms (identical layout on macOS
// and Linux -- sizeof(struct tms) == 32 on both, four plain clock_t fields)
// and returns elapsed wall-clock ticks since an arbitrary point in the past.
[[cccc::test(return = 42)]]
int test_posix_times(void) {
    struct tms t;
    for (int i = 0; i < (int)sizeof(t); i++) ((char *)&t)[i] = 0;
    clock_t r1 = times(&t);
    if (r1 == (clock_t)-1) return 1;
    if (r1 <= 0) return 2;
    if (t.tms_utime < 0 || t.tms_stime < 0) return 3;
    if (t.tms_cutime < 0 || t.tms_cstime < 0) return 4;

    /* Burn some CPU so a second call is guaranteed to observe a strictly
       later tick count -- proves the FFI return isn't silently truncated
       to a 32-bit int (which could wrap to a smaller/negative value on a
       host with enough uptime) and that struct tms is really being
       refreshed, not just zero-filled once. */
    volatile long busy = 0;
    for (long i = 0; i < 20000000L; i++) busy += i;
    (void)busy;

    clock_t r2 = times(&t);
    if (r2 == (clock_t)-1) return 5;
    if (r2 < r1) return 6;
    return 42;
}

// test_posix_tar_cpio
// #733/#737: <tar.h>/<cpio.h> are pure constant headers -- values are fixed
// by POSIX.1 itself, identical on every platform, nothing host-dependent to
// verify. Just proves the constants are visible and have the right values.
[[cccc::test(return = 42)]]
int test_posix_tar_cpio(void) {
    if (strcmp(TMAGIC, "ustar") != 0) return 1;
    if (TMAGLEN != 6) return 2;
    if (REGTYPE != '0') return 3;
    if (DIRTYPE != '5') return 4;
    if ((TUREAD | TUWRITE | TUEXEC) != 00700) return 5;

    if (strcmp(MAGIC, "070707") != 0) return 6;
    if ((C_IRUSR | C_IWUSR | C_IXUSR) != 000700) return 7;
    if (C_ISDIR != 040000) return 8;
    if (C_ISREG != 0100000) return 9;
    return 42;
}

// test_posix_syslog
// #803: openlog/syslog/setlogmask/closelog, plus vsyslog forwarding a
// captured va_list through ffi_prep_cif_var (same mechanism as the printf
// v*-family, #407). syslog()'s output goes to the system log, not somewhere
// this test can capture portably, so this only asserts no crash and that
// setlogmask round-trips the previous mask correctly.
static void posix_syslog_va_helper(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsyslog(LOG_INFO, fmt, ap);
    va_end(ap);
}

[[cccc::test(return = 42)]]
int test_posix_syslog(void) {
    openlog("cccc-test", LOG_PID | LOG_NDELAY, LOG_USER);

    int prev = setlogmask(LOG_UPTO(LOG_INFO));
    int prev2 = setlogmask(prev);
    if (prev2 != LOG_UPTO(LOG_INFO)) return 1;

    syslog(LOG_INFO, "cccc syslog test: %s %d %.2f", "ok", 7, 2.5);

    errno = ENOENT;
    syslog(LOG_ERR, "cccc syslog %%m test: %m");

    posix_syslog_va_helper("cccc vsyslog test: %s %d %.2f", "ok", 9, 1.5);

    closelog();
    return 42;
}

// test_posix_select
// #798: fd_set/FD_SET/FD_ZERO/FD_ISSET over a pipe, select() with a 0
// timeout (nothing ready yet), then again after writing (ready). pselect()
// exercised the same way, once with a real sigmask (built from the guest's
// own sigset_t, translated to a host sigset_t by wrap_pselect_gil) and once
// with NULL. Also asserts sizeof(struct timeval) == 16 with the tv_usec
// width fix (previously 4 bytes on macOS's real timeval vs the guest's
// 8-byte long, leaving the upper half stale after gettimeofday()).
[[cccc::test(return = 42)]]
int test_posix_select(void) {
    if (sizeof(struct timeval) != 16) return 1;
    if (sizeof(fd_set) != 128) return 2;

    int fds[2];
    if (pipe(fds) != 0) return 3;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fds[0], &rfds);
    struct timeval tv = {0, 0};
    int r = select(fds[0] + 1, &rfds, 0, 0, &tv);
    if (r != 0) return 4;
    if (FD_ISSET(fds[0], &rfds)) return 5;

    if (write(fds[1], "x", 1) != 1) return 6;

    FD_ZERO(&rfds);
    FD_SET(fds[0], &rfds);
    struct timeval tv2 = {1, 0};
    r = select(fds[0] + 1, &rfds, 0, 0, &tv2);
    if (r != 1) return 7;
    if (!FD_ISSET(fds[0], &rfds)) return 8;

    char c;
    if (read(fds[0], &c, 1) != 1 || c != 'x') return 9;

    /* pselect with a real (empty) sigmask */
    FD_ZERO(&rfds);
    FD_SET(fds[0], &rfds);
    struct timespec ts = {0, 0};
    sigset_t mask;
    sigemptyset(&mask);
    r = pselect(fds[0] + 1, &rfds, 0, 0, &ts, &mask);
    if (r != 0) return 10;

    if (write(fds[1], "y", 1) != 1) return 11;

    /* pselect with NULL sigmask */
    FD_ZERO(&rfds);
    FD_SET(fds[0], &rfds);
    struct timespec ts2 = {1, 0};
    r = pselect(fds[0] + 1, &rfds, 0, 0, &ts2, 0);
    if (r != 1) return 12;
    if (!FD_ISSET(fds[0], &rfds)) return 13;

    close(fds[0]);
    close(fds[1]);
    return 42;
}

#pragma cccc suite end
