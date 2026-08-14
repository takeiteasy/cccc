// posix_statfs.c -- statfs/fstatfs/statvfs/fstatvfs guest struct
// translation (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

// statfs/fstatfs (#792): include/sys/mount.h deliberately declares a
// minimal, CCCC-canonical `struct statfs` projection -- NOT the host ABI
// layout, which is enormous (~2100 bytes on macOS, mostly mount-path
// strings and per-platform extras) versus the guest's ~56-byte struct.
// Registering the real statfs()/fstatfs() directly against a pointer to
// the guest struct would have the host write far past the end of it,
// corrupting adjacent guest memory (confirmed empirically: a canary
// written just past a malloc'd guest-sized buffer gets clobbered). So
// these translate through a host-sized local and copy only the documented
// fields across, the same host-numbering-translation shape as
// wrap_sysconf/wrap_pathconf/wrap_confstr above. Unlike struct stat
// (include/sys/stat.h, byte-for-byte host-matched with a
// _Static_assert), statfs is intentionally not host-matched -- the real
// layout carries far more than any of CCCC's stdlib needs to expose.
struct cccc_guest_statfs {
    unsigned int  f_bsize;
    unsigned int  f_iosize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned int  f_flags;
};

static void copy_statfs_fields(const struct statfs *host_buf, long long guest_ptr) {
    struct cccc_guest_statfs *g = (struct cccc_guest_statfs *)(void *)guest_ptr;
    g->f_bsize  = (unsigned int)host_buf->f_bsize;
#ifdef __linux__
    // glibc's struct statfs has f_frsize, not f_iosize; closest analog.
    g->f_iosize = (unsigned int)host_buf->f_frsize;
#else
    g->f_iosize = (unsigned int)host_buf->f_iosize;
#endif
    g->f_blocks = (unsigned long)host_buf->f_blocks;
    g->f_bfree  = (unsigned long)host_buf->f_bfree;
    g->f_bavail = (unsigned long)host_buf->f_bavail;
    g->f_files  = (unsigned long)host_buf->f_files;
    g->f_ffree  = (unsigned long)host_buf->f_ffree;
    g->f_flags  = (unsigned int)host_buf->f_flags;
}

static long long wrap_statfs(long long path, long long buf) {
    struct statfs host_buf;
    int rc = statfs((const char *)path, &host_buf);
    if (rc == 0 && buf) copy_statfs_fields(&host_buf, buf);
    return rc;
}

static long long wrap_fstatfs(long long fd, long long buf) {
    struct statfs host_buf;
    int rc = fstatfs((int)fd, &host_buf);
    if (rc == 0 && buf) copy_statfs_fields(&host_buf, buf);
    return rc;
}

// sys/statvfs.h (#799) -- struct statvfs diverges even harder than statfs
// (64 bytes/32-bit counters on macOS vs 112 bytes/64-bit counters on
// Linux), so the guest gets a CCCC-canonical struct in POSIX field order
// with wide counters on both platforms, populated field-by-field from a
// host-local struct statvfs -- same shape as copy_statfs_fields above.
struct cccc_guest_statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned long f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
};

static void copy_statvfs_fields(const struct statvfs *host_buf, long long guest_ptr) {
    struct cccc_guest_statvfs *g = (struct cccc_guest_statvfs *)(void *)guest_ptr;
    g->f_bsize   = (unsigned long)host_buf->f_bsize;
    g->f_frsize  = (unsigned long)host_buf->f_frsize;
    g->f_blocks  = (unsigned long)host_buf->f_blocks;
    g->f_bfree   = (unsigned long)host_buf->f_bfree;
    g->f_bavail  = (unsigned long)host_buf->f_bavail;
    g->f_files   = (unsigned long)host_buf->f_files;
    g->f_ffree   = (unsigned long)host_buf->f_ffree;
    g->f_favail  = (unsigned long)host_buf->f_favail;
    g->f_fsid    = (unsigned long)host_buf->f_fsid;
    g->f_flag    = (unsigned long)host_buf->f_flag;
    g->f_namemax = (unsigned long)host_buf->f_namemax;
}

static long long wrap_statvfs(long long path, long long buf) {
    struct statvfs host_buf;
    int rc = statvfs((const char *)path, &host_buf);
    if (rc == 0 && buf) copy_statvfs_fields(&host_buf, buf);
    return rc;
}

static long long wrap_fstatvfs(long long fd, long long buf) {
    struct statvfs host_buf;
    int rc = fstatvfs((int)fd, &host_buf);
    if (rc == 0 && buf) copy_statvfs_fields(&host_buf, buf);
    return rc;
}

void register_posix_statfs_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "statfs",  (void*)wrap_statfs,  2, 0);
    cc_register_cfunc(vm, "fstatfs", (void*)wrap_fstatfs, 2, 0);
    cc_register_cfunc(vm, "statvfs",  (void*)wrap_statvfs,  2, 0);
    cc_register_cfunc(vm, "fstatvfs", (void*)wrap_fstatvfs, 2, 0);
}

#else
void register_posix_statfs_functions(VirtualMachine *vm) { (void)vm; }
#endif
