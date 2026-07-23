// Expected return: 42
// #409: previously-unregistered termios.h speed/line-control functions.
// No real tty needed -- operates on a local struct termios.
#include <termios.h>

int main(void) {
    struct termios t;
    cfmakeraw(&t);
    if (t.c_lflag & (ECHO | ICANON))
        return 1;

    if (cfsetspeed(&t, B9600) != 0)
        return 2;
    if (cfgetispeed(&t) != B9600)
        return 3;
    if (cfgetospeed(&t) != B9600)
        return 4;

    if (cfsetispeed(&t, B19200) != 0)
        return 5;
    if (cfgetispeed(&t) != B19200)
        return 6;
    if (cfsetospeed(&t, B38400) != 0)
        return 7;
    if (cfgetospeed(&t) != B38400)
        return 8;

    return 42;
}
