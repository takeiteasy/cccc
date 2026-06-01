#include <termios.h>
#include <unistd.h>

int main(void) {
    struct termios t;
    if (NCCS < 10) return 1;
    t.c_cc[0] = 0;

    int rc = tcgetattr(STDIN_FILENO, &t);
    if (rc == 0) {
        if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) return 2;
    }

    return 42;
}
