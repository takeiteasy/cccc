// CCCC_NATIVE_SKIP: checks the VM heap allocator
// VLA storage is still reclaimed at block exit once a thread has run: every
// loop iteration gets the same address back.
#include <threads.h>

static int worker(void *arg) {
    (void)arg;
    return 0;
}

int main(void) {
    thrd_t t;
    if (thrd_create(&t, worker, 0) != thrd_success)
        return 1;
    thrd_join(t, 0);
    int       k     = 4;
    long long first = 0;
    for (int i = 0; i < 20; i++) {
        int u[k];
        u[0]           = i;
        long long addr = (long long)&u[0];
        if (i == 0)
            first = addr;
        else if (addr != first)
            return 2;
    }
    return 42;
}
// Expected return: 42
