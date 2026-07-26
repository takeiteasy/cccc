// Regression test for #792: unlike sys/file.h and sys/ioctl.h, sys/un.h
// already worked before this fix -- it `#include "sys/socket.h"` itself,
// which is already mapped in tools/stdlib.tsv to register_posix_functions.
// The tools/stdlib.tsv row added for sys/un.h in this change is defensive
// (guards against a future refactor of sys/un.h's own includes), not a
// functional fix. This test is a straight regression guard that the
// AF_UNIX + struct sockaddr_un path keeps working when only <sys/un.h> is
// included.
#include <sys/un.h>

extern int close(int fd);
extern int unlink(const char *path);

int main(void) {
    const char *path = "/tmp/cccc_test_sys_un_standalone.sock";
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 1;

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    int i = 0;
    while (path[i] && i < (int)sizeof(addr.sun_path) - 1) {
        addr.sun_path[i] = path[i];
        i++;
    }
    addr.sun_path[i] = 0;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) return 2;

    close(fd);
    unlink(path);
    return 42;
}
