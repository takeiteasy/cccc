// Regression test for #1317: include/sys/statvfs.h had no `#ifdef __CCCC__`
// / `#include_next` hand-off, unlike its sibling include/sys/mount.h (#1031),
// despite the same layout-divergence property -- CCCC's own canonical
// `struct statvfs` projection is 88 bytes with wide (unsigned long)
// counters, versus the real host layout (64 bytes/32-bit counters on macOS,
// 112 bytes/64-bit counters on Linux). Without the hand-off, a `-I./include`
// invocation (the test harness's own tools/testing/native.py) shadows the
// real <sys/statvfs.h> under -c=native, so `sizeof(struct statvfs)` folds
// against CCCC's 88-byte projection while the real host statvfs()/
// fstatvfs() write their own, differently-sized layout -- same canary shape
// as tests/test_sys_mount_statfs.c.
//
// fstatvfs() needs an fd backed by a real mount -- NOT stdin (fd 0), which
// test_sys_mount_statfs.c originally (incorrectly) used before #847's fix;
// open "." explicitly instead, which is always mount-backed.
#include <sys/statvfs.h>
#include <fcntl.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);
extern void *memset(void *s, int c, unsigned long n);
extern int open(const char *path, int flags, ...);
extern int close(int fd);

int main(void) {
    unsigned long  guest_size = sizeof(struct statvfs);
    unsigned long  tail       = 64;
    unsigned char *buf        = (unsigned char *)malloc(guest_size + tail);
    if (!buf)
        return 1;
    memset(buf, 0xAA, guest_size + tail);

    struct statvfs *sb = (struct statvfs *)buf;
    if (statvfs("/", sb) != 0)
        return 2;
    if (sb->f_bsize == 0)
        return 3;

    for (unsigned long i = guest_size; i < guest_size + tail; i++) {
        if (buf[i] != 0xAA) {
            free(buf);
            return 4; // canary clobbered: statvfs overran the guest struct
        }
    }

    // fstatvfs, same canary shape.
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

    struct statvfs *sb2 = (struct statvfs *)buf2;
    if (fstatvfs(fd, sb2) != 0) {
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
