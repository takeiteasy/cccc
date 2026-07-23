// CCCC_FLAGS: --testing
// Consolidated suite: POSIX: unistd, dirent, glob, regex, socket, mman, etc.
// Source tests: test_posix_arpa_inet, test_posix_dirent, test_posix_extra_ffi, test_posix_fnmatch, test_posix_glob, test_posix_libgen, test_posix_poll, test_posix_pwd_grp, test_posix_regex, test_posix_socket_netdb, test_posix_strings, test_posix_sys_mman, test_posix_sys_stat, test_posix_sys_time, test_posix_termios, test_posix_unistd_fcntl, test_posix_utime, test_posix_vfs_decls,
//   test_glob_header, test_quick_exit, test_posix_sys_wait,
//   test_posix_sysconf_pathconf_confstr, test_posix_host_global_bridge

#include <arpa/inet.h>
#include <dirent.h>
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
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <strings.h>
#include <sys/mman.h>
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
        (void*)pread, (void*)pwrite, (void*)fdatasync, (void*)getpagesize,
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

    raise(SIGUSR1);
    if (sigaction_got_it != SIGUSR1) return 3;

    struct sigaction q;
    for (int i = 0; i < (int)sizeof(q); i++) ((char *)&q)[i] = 0;
    if (sigaction(SIGUSR1, 0, &q) != 0) return 4;
    if (q.sa_handler != sigaction_handler) return 5;
    if (q.sa_flags != 0x1234) return 6;
    if (sigismember(&q.sa_mask, SIGTERM) != 1) return 7;

    struct sigaction dfl;
    for (int i = 0; i < (int)sizeof(dfl); i++) ((char *)&dfl)[i] = 0;
    dfl.sa_handler = SIG_DFL;
    if (sigaction(SIGUSR1, &dfl, 0) != 0) return 8;

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

#pragma cccc suite end
