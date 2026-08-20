// Regression test for #792: flock() is declared in include/sys/file.h but
// was never registered via cc_register_cfunc anywhere, so a guest program
// calling it failed with "undefined function: flock" -- even though
// sys/file.h already reaches register_posix_functions transitively via its
// own `#include "fcntl.h"`. Fixed by registering flock in
// register_posix_functions (src/stdlib/posix.c); the tools/stdlib.tsv row
// added alongside it is defensive (sys/file.h already worked via fcntl.h).
//
// Includes ONLY <sys/file.h> (which pulls in fcntl.h/unistd.h itself) to
// mirror the exact repro from the ticket: "flock() from sys/file.h ... works
// without also including an unrelated header."
#include <sys/file.h>

int main(void) {
    const char *path = "/tmp/cccc_test_sys_file_standalone";
    unlink(path);

    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0)
        return 1;

    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
        return 2;
    if (flock(fd, LOCK_UN) != 0)
        return 3;

    close(fd);
    unlink(path);
    return 42;
}
