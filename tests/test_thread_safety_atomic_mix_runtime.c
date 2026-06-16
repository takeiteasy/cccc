// CCCC_FLAGS: --thread-safety
// CCCC_EXPECT_STDERR: MIXED ATOMIC/NON-ATOMIC ACCESS DETECTED
// Runtime detection of mixed atomic/non-atomic access to the same address.
// The main thread performs an atomic_store (tags the address in atomic_shadow),
// then a worker thread reads the same address via a plain (non-atomic) dereference
// from a different thread without a mutex. This triggers the mixed-access diagnostic.
#include <stdatomic.h>
#include <pthread.h>

static _Atomic int shared = 0;

static void *plain_read(void *arg) {
    (void)arg;
    // Cast away _Atomic and do a plain non-atomic read.
    // Different thread + no mutex + address was previously atomic-accessed → warning.
    int *p = (int *)&shared;
    int v = *p;  // plain LDR_W — triggers MIXED ATOMIC/NON-ATOMIC warning
    return (void *)(long long)v;
}

int main(void) {
    // Main thread: atomic store tags the address in atomic_shadow
    atomic_store(&shared, 1);

    pthread_t t;
    if (pthread_create(&t, 0, plain_read, 0) != 0)
        return 1;
    pthread_join(t, 0);
    return 42; // CCCC test convention: 42 = pass
}
