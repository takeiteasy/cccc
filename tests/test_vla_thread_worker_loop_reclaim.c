// CCCC_NATIVE_SKIP: checks the VM heap allocator
// Workers yielding to each other between VLA blocks each get their block's
// storage back at block exit, so the address is the same every iteration.
#include <stdatomic.h>
#include <threads.h>

static atomic_int g_bad;

static int worker(void *arg) {
    int       k     = *(int *)arg;
    long long first = 0;
    for (int i = 0; i < 20; i++) {
        {
            int u[k];
            u[0]           = i;
            long long addr = (long long)&u[0];
            if (i == 0)
                first = addr;
            else if (addr != first)
                atomic_fetch_add(&g_bad, 1);
        }
        thrd_yield();
    }
    return 0;
}

int main(void) {
    thrd_t t[2];
    int    k[2] = {4, 9};
    for (int i = 0; i < 2; i++)
        if (thrd_create(&t[i], worker, &k[i]) != thrd_success)
            return 1;
    for (int i = 0; i < 2; i++)
        thrd_join(t[i], 0);
    return atomic_load(&g_bad) == 0 ? 42 : 2;
}
// Expected return: 42
