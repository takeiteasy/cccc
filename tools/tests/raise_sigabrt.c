#include <signal.h>
int main(void) {
    raise(SIGABRT);
    return 42;
}
