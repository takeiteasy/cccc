// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int thrd_create\(thrd_t \*thr, thrd_start_t func, void \*arg\) \{
// CCCC_EXPECT_STDOUT: void __cccc_call_once\(__cccc_once_flag \*flag, void \(\*func\)\(void\)\) \{
// CCCC_REJECT_STDOUT: #define once_flag
// CCCC_REJECT_STDOUT: #define call_once
// CCCC_REJECT_STDOUT: typedef int once_flag;
// CCCC_REJECT_STDOUT: typedef _Atomic int once_flag;
// CCCC_REJECT_STDOUT: void call_once\(
// -m output defines the thrd_* and call_once shims under their private names.
#include <threads.h>
#include <stdatomic.h>
#include <time.h>
static mtx_t       mutex;
static cnd_t       cond;
static int         ready        = 0;
static int         shared_val   = 0;
static _Atomic int woke_count   = 0;
static _Atomic int detached_ran = 0;
static once_flag   once         = ONCE_FLAG_INIT;
static int         once_calls   = 0;
static void init_once(void) {
    once_calls++;
}
static int waiter(void *arg) {
    (void)arg;
    mtx_lock(&mutex);
    while (!ready)
        cnd_wait(&cond, &mutex);
    atomic_fetch_add(&woke_count, 1);
    mtx_unlock(&mutex);
    return 0;
}
static int broadcaster(void *arg) {
    (void)arg;
    call_once(&once, init_once);
    mtx_lock(&mutex);
    shared_val = 1;
    ready      = 1;
    cnd_broadcast(&cond);
    mtx_unlock(&mutex);
    return 0;
}
static int detached_worker(void *arg) {
    (void)arg;
    atomic_store(&detached_ran, 1);
    thrd_exit(7);
    return 99;
}
int main(void) {
    if (mtx_init(&mutex, mtx_plain) != thrd_success)
        return 1;
    if (cnd_init(&cond) != thrd_success)
        return 2;

    thrd_t w1, w2, b;
    if (thrd_create(&w1, waiter, 0) != thrd_success)
        return 3;
    if (thrd_create(&w2, waiter, 0) != thrd_success)
        return 4;
    struct timespec nap = {0, 20000000};
    thrd_sleep(&nap, 0);
    if (thrd_create(&b, broadcaster, 0) != thrd_success)
        return 5;
    thrd_join(w1, 0);
    thrd_join(w2, 0);
    thrd_join(b, 0);
    if (shared_val != 1 || atomic_load(&woke_count) != 2)
        return 6;
    if (once_calls != 1)
        return 7;

    if (mtx_trylock(&mutex) != thrd_success)
        return 8;
    mtx_unlock(&mutex);

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    ts.tv_sec += 1;
    if (mtx_timedlock(&mutex, &ts) != thrd_success)
        return 9;
    mtx_unlock(&mutex);

    struct timespec deadline;
    timespec_get(&deadline, TIME_UTC);
    deadline.tv_nsec += 20000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec  += 1;
        deadline.tv_nsec -= 1000000000;
    }
    mtx_lock(&mutex);
    int rc = cnd_timedwait(&cond, &mutex, &deadline);
    mtx_unlock(&mutex);
    if (rc != thrd_timedout)
        return 10;

    thrd_t self = thrd_current();
    if (!thrd_equal(self, thrd_current()))
        return 11;
    if (thrd_equal(w1, w2))
        return 12;

    thrd_t t3;
    if (thrd_create(&t3, detached_worker, 0) != thrd_success)
        return 13;
    if (thrd_detach(t3) != thrd_success)
        return 14;
    for (int i = 0; i < 1000 && !atomic_load(&detached_ran); i++) {
        struct timespec poll_nap = {0, 1000000};
        thrd_sleep(&poll_nap, 0);
    }
    if (!atomic_load(&detached_ran))
        return 15;

    cnd_destroy(&cond);
    mtx_destroy(&mutex);
    return 42;
}
