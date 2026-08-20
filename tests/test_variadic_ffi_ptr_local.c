// Ticket V010: a variadic-registered FFI call (fcntl(fd, cmd, ...), same
// registration shape as open()/execl()/syslog()/mq_open()) taking the
// address of a stack-local aggregate as its variadic argument was reported
// to corrupt stack-tag/CHKP bookkeeping under the default safety tier,
// surfacing as a delayed segfault in later, unrelated code. Extensive
// retesting (macOS fcntl/ioctl probes, repeated full-suite runs in both
// Linux containers) could not reproduce it -- the ticket's "-0 makes it
// disappear" evidence turned out to be confounded (default tier and -0
// produce a bit-identical flags word, src/main.c). This is the regression
// guard: repeat the exact pattern many times so a real corruption would
// show up as a crash, not a soft failure.
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char tmpl[] = "/tmp/cccc_variadic_ptr_local_XXXXXX";
    int  fd     = mkstemp(tmpl);
    if (fd < 0)
        return 1;

    for (int i = 0; i < 500; i++) {
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type   = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start  = 0;
        fl.l_len    = 0;
        if (fcntl(fd, F_SETLK, &fl) != 0) {
            close(fd);
            unlink(tmpl);
            return 2;
        }
        fl.l_type = F_UNLCK;
        if (fcntl(fd, F_SETLK, &fl) != 0) {
            close(fd);
            unlink(tmpl);
            return 3;
        }
    }

    close(fd);
    unlink(tmpl);
    return 42;
}
