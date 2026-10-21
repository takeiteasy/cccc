// CCCC_FLAGS: --testing
// CCCC_LEAKS_SKIP: guest pthread handles are never released (#1730)
// Suite: per-thread state across real thread interleaving. Every blocking
// primitive (thrd_sleep, mtx_lock, cnd_wait, the pthread equivalents, join)
// hands the VM to another thread; after each one returns, the thread must see
// its own _Thread_local, thrd_current(), tss value and stack locals again.
// Handshakes force the interleaving; no assertion depends on timing.

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <threads.h>
#include <time.h>

#pragma cccc suite begin "threads"

enum { NTHREADS = 4, ROUNDS = 16 };

_Thread_local int t_id;
static tss_t      s_key;
static atomic_int s_bad;
static int        s_turn;
static mtx_t      s_mtx;
static cnd_t      s_cnd;

static void become(int id) {
    t_id = id;
    tss_set(s_key, (void *)(intptr_t)id);
}

static void check_self(int id, thrd_t self, int local) {
    if (t_id != id || !thrd_equal(thrd_current(), self) ||
        tss_get(s_key) != (void *)(intptr_t)id || local != id * 7)
        atomic_fetch_add(&s_bad, 1);
}

static int next_turn(int id) {
    return id % NTHREADS + 1;
}

static void setup(void) {
    atomic_store(&s_bad, 0);
    s_turn = 1;
    tss_create(&s_key, 0);
    mtx_init(&s_mtx, mtx_plain);
    cnd_init(&s_cnd);
}

static void teardown(void) {
    cnd_destroy(&s_cnd);
    mtx_destroy(&s_mtx);
    tss_delete(s_key);
}

// Starts NTHREADS workers with ids 1..NTHREADS and joins them. Main keeps
// t_id == -1 across every join.
static int run_c11(thrd_start_t fn) {
    thrd_t th[NTHREADS];
    int    ids[NTHREADS];
    t_id = -1;
    for (int i = 0; i < NTHREADS; i++) {
        ids[i] = i + 1;
        if (thrd_create(&th[i], fn, &ids[i]) != thrd_success)
            return 1;
    }
    for (int i = 0; i < NTHREADS; i++) {
        thrd_join(th[i], 0);
        if (t_id != -1)
            atomic_fetch_add(&s_bad, 1);
    }
    return atomic_load(&s_bad) == 0 ? 42 : 2;
}

static int sleep_worker(void *arg) {
    int    id    = *(int *)arg;
    int    local = id * 7;
    thrd_t self  = thrd_current();
    become(id);
    for (int r = 0; r < ROUNDS; r++) {
        while (__atomic_load_n(&s_turn, __ATOMIC_ACQUIRE) != id)
            thrd_sleep(&(struct timespec){.tv_nsec = 100000}, 0);
        check_self(id, self, local);
        __atomic_store_n(&s_turn, next_turn(id), __ATOMIC_RELEASE);
    }
    return 0;
}

[[cccc::test(return = 42)]]
int test_threads_tls_across_thrd_sleep(void) {
    setup();
    int rc = run_c11(sleep_worker);
    teardown();
    return rc;
}

static int s_counter;

// Sleeping while holding the mutex parks the other workers inside mtx_lock.
static int mutex_worker(void *arg) {
    int    id    = *(int *)arg;
    int    local = id * 7;
    thrd_t self  = thrd_current();
    become(id);
    for (int r = 0; r < ROUNDS; r++) {
        mtx_lock(&s_mtx);
        check_self(id, self, local);
        int seen = s_counter;
        thrd_sleep(&(struct timespec){.tv_nsec = 50000}, 0);
        s_counter = seen + 1;
        check_self(id, self, local);
        mtx_unlock(&s_mtx);
    }
    return 0;
}

[[cccc::test(return = 42)]]
int test_threads_tls_across_mtx_lock(void) {
    setup();
    s_counter = 0;
    int rc    = run_c11(mutex_worker);
    teardown();
    return rc == 42 && s_counter == NTHREADS * ROUNDS ? 42 : rc;
}

static int cond_rounds = ROUNDS;

static int cond_worker(void *arg) {
    int    id    = *(int *)arg;
    int    local = id * 7;
    thrd_t self  = thrd_current();
    become(id);
    for (int r = 0; r < cond_rounds; r++) {
        mtx_lock(&s_mtx);
        while (s_turn != id)
            cnd_wait(&s_cnd, &s_mtx);
        check_self(id, self, local);
        s_turn = next_turn(id);
        cnd_broadcast(&s_cnd);
        mtx_unlock(&s_mtx);
    }
    return 0;
}

[[cccc::test(return = 42)]]
int test_threads_tls_across_cnd_wait(void) {
    setup();
    cond_rounds = ROUNDS;
    int rc      = run_c11(cond_worker);
    teardown();
    return rc;
}

static pthread_mutex_t p_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  p_cnd = PTHREAD_COND_INITIALIZER;

static void *pthread_worker(void *arg) {
    int       id    = *(int *)arg;
    int       local = id * 7;
    pthread_t self  = pthread_self();
    become(id);
    for (int r = 0; r < ROUNDS; r++) {
        pthread_mutex_lock(&p_mtx);
        while (s_turn != id)
            pthread_cond_wait(&p_cnd, &p_mtx);
        if (t_id != id || !pthread_equal(pthread_self(), self) ||
            tss_get(s_key) != (void *)(intptr_t)id || local != id * 7)
            atomic_fetch_add(&s_bad, 1);
        s_turn = next_turn(id);
        pthread_cond_broadcast(&p_cnd);
        pthread_mutex_unlock(&p_mtx);
    }
    return (void *)(intptr_t)id;
}

[[cccc::test(return = 42)]]
int test_threads_tls_across_pthread_cond_wait(void) {
    setup();
    pthread_t th[NTHREADS];
    int       ids[NTHREADS];
    t_id = -1;
    for (int i = 0; i < NTHREADS; i++) {
        ids[i] = i + 1;
        if (pthread_create(&th[i], 0, pthread_worker, &ids[i]) != 0)
            return 1;
    }
    for (int i = 0; i < NTHREADS; i++) {
        void *ret = 0;
        pthread_join(th[i], &ret);
        if (ret != (void *)(intptr_t)ids[i] || t_id != -1)
            atomic_fetch_add(&s_bad, 1);
    }
    teardown();
    return atomic_load(&s_bad) == 0 ? 42 : 2;
}

[[cccc::test(return = 42)]]
int test_threads_create_interleave_join_stress(void) {
    setup();
    cond_rounds = 4;
    int rc      = 42;
    for (int round = 0; round < 8 && rc == 42; round++) {
        s_turn = 1;
        rc     = run_c11(cond_worker);
    }
    teardown();
    return rc;
}

#pragma cccc suite end
