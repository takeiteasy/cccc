// A bundled prototype still needed is emitted and round-trips under -c=native.
#include <sys/wait.h>
static volatile int g_never_1297b = 0;
int main(void) {
    int p = getpriority(0, 0);
    (void)p;
    if (g_never_1297b) {
        setpriority(0, 0, 0);
    }
    return 42;
}
