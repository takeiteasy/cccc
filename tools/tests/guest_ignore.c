#include <signal.h>
int main(void) {
    signal(SIGSEGV, SIG_IGN);
    volatile int *p     = (volatile int *)0;
    volatile int  value = *p;
    (void)value;
    return 42;
}
