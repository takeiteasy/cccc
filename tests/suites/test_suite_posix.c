// CCCC_FLAGS: --testing
// Consolidated suite: POSIX: unistd, dirent, glob, regex, socket, mman, etc.
// Source tests: test_posix_arpa_inet, test_posix_dirent, test_posix_extra_ffi, test_posix_fnmatch, test_posix_glob, test_posix_libgen, test_posix_poll, test_posix_pwd_grp, test_posix_regex, test_posix_socket_netdb, test_posix_strings, test_posix_sys_mman, test_posix_sys_stat, test_posix_sys_time, test_posix_termios, test_posix_unistd_fcntl, test_posix_utime, test_posix_vfs_decls,
//   test_glob_header, test_quick_exit, test_posix_sys_wait

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

#pragma cccc suite end
