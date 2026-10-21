// CCCC_NATIVE_SKIP: checks the VM heap allocator
// CCCC_LEAKS_SKIP: guest pthread handles are never released (#1730)
// A block-exit rewind on one thread never overwrites another thread's live
// VLA. B's block mark is taken while A's first VLA sits below it; A then
// frees that VLA and allocates a larger one across B's mark. B's rewind to
// its own mark must stop at the end of A's VLA.
#include <threads.h>

enum { N = 16 };

static mtx_t g_mtx;
static cnd_t g_cnd;
static int   g_stage;

static void hand_to(int stage) {
    mtx_lock(&g_mtx);
    g_stage = stage;
    cnd_broadcast(&g_cnd);
    mtx_unlock(&g_mtx);
}

static void wait_for(int stage) {
    mtx_lock(&g_mtx);
    while (g_stage != stage)
        cnd_wait(&g_cnd, &g_mtx);
    mtx_unlock(&g_mtx);
}

static void fill(int *p, int n, int v) {
    for (int i = 0; i < n; i++)
        p[i] = v;
}

static int thread_a(void *arg) {
    int n = *(int *)arg;
    {
        int a1[n];
        fill(a1, n, 0xA1);
        hand_to(1);
        wait_for(2);
    }
    {
        int a2[2 * n];
        fill(a2, 2 * n, 0xA2);
        hand_to(3);
        wait_for(4);
        for (int i = 0; i < 2 * n; i++)
            if (a2[i] != 0xA2)
                return 2;
    }
    return 42;
}

static int thread_b(void *arg) {
    int n = *(int *)arg;
    wait_for(1);
    {
        hand_to(2);
        wait_for(3);
        int b1[n];
        fill(b1, n, 0xB1);
    }
    {
        int b2[2 * n];
        fill(b2, 2 * n, 0xB2);
    }
    hand_to(4);
    return 42;
}

int main(void) {
    mtx_init(&g_mtx, mtx_plain);
    cnd_init(&g_cnd);
    int    n = N;
    thrd_t a, b;
    if (thrd_create(&a, thread_a, &n) != thrd_success ||
        thrd_create(&b, thread_b, &n) != thrd_success)
        return 1;
    int ra = 0, rb = 0;
    thrd_join(a, &ra);
    thrd_join(b, &rb);
    return ra == 42 && rb == 42 ? 42 : 3;
}
// Expected return: 42
