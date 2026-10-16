// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^ok 1 - spawns
// CCCC_EXPECT_STDERR: ^ok 2 - vla_while_worker_ends
// CCCC_EXPECT_STDERR: ^ok 3 - vla_after_worker_ends
// A worker that ends while later tests run VLA code does not unbalance the
// heap-reclaim marks.
#include <pthread.h>
#include <unistd.h>

static void *worker(void *arg) {
    usleep(30000);
    return arg;
}

static int sum_vla(int n) {
    int v[n];
    if (n == 32)
        usleep(100);
    for (int i = 0; i < n; i++)
        v[i] = i;
    int s = 0;
    for (int i = 0; i < n; i++)
        s += v[i];
    return s;
}

[[cccc::test]]
void spawns(void) {
    pthread_t t;
    pthread_create(&t, 0, worker, 0);
    pthread_detach(t);
}

[[cccc::test]]
void vla_while_worker_ends(void) {
    for (int i = 0; i < 400; i++) {
        Assert(sum_vla(32) == 496);
        usleep(200);
    }
}

[[cccc::test]]
void vla_after_worker_ends(void) {
    usleep(60000);
    for (int i = 0; i < 100; i++)
        Assert(sum_vla(8) == 28);
}
