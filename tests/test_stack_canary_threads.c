// CCCC_FLAGS: --stack-canaries
// #445 — stack canaries must work alongside threading. Each thread runs
// functions with parameters; the canary-shifted frame layout must hold per
// thread without corrupting params/locals.
#include <pthread.h>

static int add(int a, int b) {
    int local = a;
    return local + b;
}

static void *worker(void *arg) {
    int n = (int)(long long)arg;
    // Exercise a few param-carrying calls inside the thread.
    long long acc = 0;
    for (int i = 0; i < 100; i++)
        acc += add(n, i);
    return (void *)acc;
}

int main(void) {
    pthread_t a, b;
    if (pthread_create(&a, 0, worker, (void *)(long long)10) != 0)
        return 1;
    if (pthread_create(&b, 0, worker, (void *)(long long)20) != 0)
        return 2;
    void *ra = 0, *rb = 0;
    pthread_join(a, &ra);
    pthread_join(b, &rb);
    // worker(n) = sum_{i=0..99}(n + i) = 100*n + 4950
    if ((long long)ra != 100 * 10 + 4950)
        return 3;
    if ((long long)rb != 100 * 20 + 4950)
        return 4;
    // And a param-carrying call on the main thread too.
    if (add(20, 22) != 42)
        return 5;
    return 42;
}
