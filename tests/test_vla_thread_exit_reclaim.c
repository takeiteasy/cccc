// CCCC_NATIVE_SKIP: checks the VM heap allocator
// A worker leaving through pthread_exit from inside a VLA-holding callee skips
// its frames' exits; its VLA storage is still reclaimed when the thread ends,
// so main's next VLA lands where its pre-thread one did.
#include <pthread.h>

static void leave_with_vla(int k) {
    int u[k];
    u[0] = k;
    pthread_exit((void *)(long)u[0]);
}

static void *worker(void *arg) {
    leave_with_vla((int)(long)arg);
    return 0;
}

static long long vla_addr(int k) {
    int u[k];
    u[0] = k;
    return (long long)&u[0];
}

int main(void) {
    long long before = vla_addr(4);
    pthread_t t;
    void     *ret = 0;
    if (pthread_create(&t, 0, worker, (void *)64L) != 0)
        return 1;
    pthread_join(t, &ret);
    if ((long)ret != 64)
        return 2;
    return vla_addr(4) == before ? 42 : 3;
}
// Expected return: 42
