#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <errno.h>

int main(void) {
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
