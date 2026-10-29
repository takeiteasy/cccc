// <sys/time.h> makes fd_set and FD_* visible, as glibc and Darwin do.
#include <sys/time.h>

int main(void) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(3, &set);
    return FD_ISSET(3, &set) && !FD_ISSET(4, &set) ? 42 : 1;
}
