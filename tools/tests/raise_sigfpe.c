#include <signal.h>
int main(void) {
    raise(SIGFPE);
    return 42;
}
