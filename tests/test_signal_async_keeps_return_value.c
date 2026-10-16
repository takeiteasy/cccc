// A guest signal handler delivered right after a call returns must not change
// that call's result: the interrupted registers are restored on handler return.
#include <signal.h>
#include <unistd.h>

static volatile int seen;

static void handler(int sig) {
    (void)sig;
    seen = 1;
}

int main(void) {
    signal(SIGUSR1, handler);
    for (int i = 0; i < 50; i++) {
        seen   = 0;
        int rc = kill(getpid(), SIGUSR1);
        if (rc != 0)
            return 1;
        for (int spin = 0; !seen && spin < 100000; spin++)
            ;
        if (!seen)
            return 2;
    }
    return 42;
}
