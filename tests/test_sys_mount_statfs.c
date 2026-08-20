// Regression test for #792 (and a bug found while fixing it): statfs()/
// fstatfs() are declared in include/sys/mount.h but were never registered
// via cc_register_cfunc anywhere -- same "undefined function" gap class as
// flock/ioctl above, surfaced by widening tools/audit_ffi.py's header scan.
//
// The naive fix (registering the real host statfs()/fstatfs() directly
// against a pointer to the guest's minimal `struct statfs`) is a serious
// bug: the host struct is enormous (~2100 bytes on macOS, mostly
// mount-path strings) versus the guest's ~56-byte projection, so the host
// call writes far past the end of the guest buffer and corrupts whatever
// follows it in guest memory. This test is the canary that catches that
// regression: it allocates a buffer sized to exactly the guest struct plus
// a 64-byte tail, poisons the whole thing, calls statfs(), and asserts the
// tail is untouched. src/stdlib/posix.c's wrap_statfs/wrap_fstatfs
// translate through a host-sized local and copy only the documented
// fields across, which is what keeps this test passing.
//
// fstatfs() needs an fd backed by a real mount -- NOT stdin (fd 0), which
// this test originally (incorrectly) used: fd 0 is only mount-backed when
// the test happens to run with a terminal or regular file attached, and
// legitimately fails with ENOTTY/EBADF whenever it's a pipe instead (any
// subprocess chain that doesn't explicitly wire a real file to stdin --
// RunCustom's vendored shell, most CI runners, `foo | tee log`, ...; see
// #847). Open "." explicitly instead, which is always mount-backed.
//
// #1031: `sizeof(struct statfs)`/`_Alignof(struct statfs)` used to fold
// guest-side against CCCC's own ~56-byte projection and stay that way in
// -c=native's emitted C, so the malloc() below undersized the buffer for
// the real host statfs()/fstatfs() (~2100 bytes on macOS) -- this test's
// own canary is what caught it (see man/HEADERS.md's writeup for the
// general fix: type_layout_is_host_owned()/serialize_expr's ND_NUM case,
// src/serialize.c). include/sys/mount.h also needed a real
// #ifdef __CCCC__ / #include_next hand-off to the real host header
// (same #1022-style shape as pthread.h) -- without it, -I./include (the
// test harness's own tools/testing/native.py invocation) shadowed the
// real <sys/mount.h> with CCCC's own copy again, defeating the fix the
// same way #1022 found for pthread.h.
#include <sys/mount.h>
#include <fcntl.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);
extern void *memset(void *s, int c, unsigned long n);
extern int open(const char *path, int flags, ...);
extern int close(int fd);

int main(void) {
    unsigned long  guest_size = sizeof(struct statfs);
    unsigned long  tail       = 64;
    unsigned char *buf        = (unsigned char *)malloc(guest_size + tail);
    if (!buf)
        return 1;
    memset(buf, 0xAA, guest_size + tail);

    struct statfs *sb = (struct statfs *)buf;
    if (statfs("/", sb) != 0)
        return 2;
    if (sb->f_bsize == 0)
        return 3;

    for (unsigned long i = guest_size; i < guest_size + tail; i++) {
        if (buf[i] != 0xAA) {
            free(buf);
            return 4; // canary clobbered: statfs overran the guest struct
        }
    }

    // fstatfs, same canary shape.
    unsigned char *buf2 = (unsigned char *)malloc(guest_size + tail);
    if (!buf2) {
        free(buf);
        return 5;
    }
    memset(buf2, 0xAA, guest_size + tail);

    int fd = open(".", O_RDONLY);
    if (fd < 0) {
        free(buf);
        free(buf2);
        return 8;
    }

    struct statfs *sb2 = (struct statfs *)buf2;
    if (fstatfs(fd, sb2) != 0) {
        close(fd);
        free(buf);
        free(buf2);
        return 6;
    }
    close(fd);
    for (unsigned long i = guest_size; i < guest_size + tail; i++) {
        if (buf2[i] != 0xAA) {
            free(buf);
            free(buf2);
            return 7;
        }
    }

    free(buf);
    free(buf2);
    return 42;
}
