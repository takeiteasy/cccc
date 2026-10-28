// CCCC_FLAGS: --testing
// Plain libc functions the fortified headers reach: strlcpy/strlcat, dprintf,
// readlinkat, getwd, and the glibc-only unlocked and explicit_bzero entry
// points.

// glibc declares the unlocked and explicit_bzero entry points under this.
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

[[cccc::test]]
void test_strlcpy_truncates_and_returns_source_length(void) {
    char a[4];
    AssertEq(strlcpy(a, "abcdef", sizeof a), 6);
    AssertStrEq(a, "abc");
    AssertEq(strlcpy(a, "x", sizeof a), 1);
    AssertStrEq(a, "x");
    AssertEq(strlcpy(a, "abc", 0), 3);
}

[[cccc::test]]
void test_strlcat_appends_within_the_buffer(void) {
    char a[8] = "ab";
    AssertEq(strlcat(a, "cd", sizeof a), 4);
    AssertStrEq(a, "abcd");
    AssertEq(strlcat(a, "efghij", sizeof a), 10);
    AssertStrEq(a, "abcdefg");
}

static int vdprintf_wrap(int fd, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vdprintf(fd, fmt, ap);
    va_end(ap);
    return r;
}

[[cccc::test]]
void test_dprintf_and_vdprintf_write_to_the_fd(void) {
    int fds[2];
    AssertEq(pipe(fds), 0);
    AssertEq(dprintf(fds[1], "%d-%s", 12, "ab"), 5);
    AssertEq(vdprintf_wrap(fds[1], "|%03d", 7), 4);
    close(fds[1]);
    char buf[16] = {0};
    AssertEq(read(fds[0], buf, sizeof buf - 1), 9);
    AssertStrEq(buf, "12-ab|007");
    close(fds[0]);
}

[[cccc::test]]
void test_readlinkat_resolves_a_link(void) {
    char dir[] = "/tmp/cccc_rlat_XXXXXX";
    AssertNotNull(mkdtemp(dir));
    char link[64], target[] = "target-name";
    snprintf(link, sizeof link, "%s/ln", dir);
    AssertEq(symlink(target, link), 0);
    int dfd = open(dir, O_RDONLY);
    AssertTrue(dfd >= 0);
    char buf[32] = {0};
    AssertEq(readlinkat(dfd, "ln", buf, sizeof buf - 1), (long)strlen(target));
    AssertStrEq(buf, target);
    close(dfd);
    unlink(link);
    rmdir(dir);
}

[[cccc::test]]
void test_getwd_matches_getcwd(void) {
    char a[4096], b[4096];
    AssertNotNull(getwd(a));
    AssertNotNull(getcwd(b, sizeof b));
    AssertStrEq(a, b);
}

#ifdef __linux__
[[cccc::test]]
void test_explicit_bzero_clears(void) {
    char a[8] = "secret";
    explicit_bzero(a, sizeof a);
    AssertEq(a[0], 0);
    AssertEq(a[5], 0);
}

[[cccc::test]]
void test_unlocked_readers(void) {
    FILE *f = tmpfile();
    AssertNotNull(f);
    fputs("abcdefgh\nwxyz\n", f);
    rewind(f);
    char line[16];
    AssertNotNull(fgets_unlocked(line, sizeof line, f));
    AssertStrEq(line, "abcdefgh\n");
    char chunk[4];
    AssertEq(fread_unlocked(chunk, 1, 4, f), 4);
    AssertEq(chunk[3], 'z');
    fclose(f);
}
#endif
