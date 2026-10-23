typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  c;
    int             n, waiting;
    unsigned        gen;
} __cccc_barrier_t;

static void __cccc_barrier_init(__cccc_barrier_t *b, int n) {
    pthread_mutex_init(&b->m, 0);
    pthread_cond_init(&b->c, 0);
    b->n       = n;
    b->waiting = 0;
    b->gen     = 0;
}

static void __cccc_barrier_destroy(__cccc_barrier_t *b) {
    pthread_mutex_destroy(&b->m);
    pthread_cond_destroy(&b->c);
}

__attribute__((unused)) static void __cccc_barrier_wait(__cccc_barrier_t *b) {
    pthread_mutex_lock(&b->m);
    unsigned gen = b->gen;
    if (++b->waiting == b->n) {
        b->waiting = 0;
        b->gen++;
        pthread_cond_broadcast(&b->c);
    } else {
        while (gen == b->gen)
            pthread_cond_wait(&b->c, &b->m);
    }
    pthread_mutex_unlock(&b->m);
}

__attribute__((unused)) static void __cccc_barrier_leave(__cccc_barrier_t *b) {
    pthread_mutex_lock(&b->m);
    b->n--;
    if (b->n > 0 && b->waiting == b->n) {
        b->waiting = 0;
        b->gen++;
        pthread_cond_broadcast(&b->c);
    }
    pthread_mutex_unlock(&b->m);
}

struct __cccc_taskrt;

struct __cccc_team {
    int              n;
    __cccc_barrier_t bar;
    int              single_done;
    void            *copyprivate;
    long             ordered; // iterations whose ordered regions have finished
    long             dyn_next; // next unclaimed index across dynamic/guided loops
    struct __cccc_doacross *da; // doacross loops in progress, under om
    pthread_mutex_t  om;
    pthread_cond_t   oc;
    // Deferred tasks (omp/tasks.c). tm guards the dependence graph, the
    // priority list, the barrier and the idle threads' sleep on tc.
    pthread_mutex_t  tm;
    pthread_cond_t   tc;
    struct __cccc_task   *hq, *hqt;
    struct __cccc_taskrt *rt; // deques and wait slots, made by the first deferred task
    void (*rt_free)(struct __cccc_team *);
    int              live, nhigh, sleepers, nwait, starved, barrived;
    unsigned         bgen;
    void (*fn)(void *);
    void *env;
};

static __thread struct __cccc_team *__cccc_tls_team;
static __thread int                 __cccc_tls_tid;
static __thread int                 __cccc_tls_single;
static __thread long                __cccc_tls_ordered_base;
static __thread long                __cccc_tls_ordered_from = -1;
static __thread long                __cccc_tls_dyn_base;
static __thread long                __cccc_tls_da_seq;
static __thread struct __cccc_doacross *__cccc_tls_da;
static __thread struct __cccc_task *__cccc_tls_task;

static struct {
    pthread_mutex_t     m;
    pthread_cond_t      wake, done;
    int                 started, pending, n;
    unsigned            gen;
    struct __cccc_team *team;
} __cccc_pool = {PTHREAD_MUTEX_INITIALIZER,
                 PTHREAD_COND_INITIALIZER,
                 PTHREAD_COND_INITIALIZER,
                 0,
                 0,
                 0,
                 0,
                 0};

static int __cccc_pool_requested;

int __cccc_pool_default_threads(void) {
    if (__cccc_pool_requested > 0)
        return __cccc_pool_requested;
    const char *env = getenv("OMP_NUM_THREADS");
    if (env && atoi(env) > 0)
        return atoi(env);
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

static void __cccc_team_run(struct __cccc_team *t, int tid) {
    struct __cccc_team *saved_team   = __cccc_tls_team;
    int                 saved_tid    = __cccc_tls_tid;
    int                 saved_single = __cccc_tls_single;
    long                saved_obase  = __cccc_tls_ordered_base;
    long                saved_ofrom  = __cccc_tls_ordered_from;
    long                saved_dbase  = __cccc_tls_dyn_base;
    long                saved_dseq   = __cccc_tls_da_seq;
    struct __cccc_doacross *saved_da = __cccc_tls_da;
    struct __cccc_task     *saved_task = __cccc_tls_task;
    __cccc_tls_team                  = t;
    __cccc_tls_tid                   = tid;
    __cccc_tls_single                = 0;
    __cccc_tls_ordered_base          = 0;
    __cccc_tls_ordered_from          = -1;
    __cccc_tls_dyn_base              = 0;
    __cccc_tls_da_seq                = 0;
    __cccc_tls_da                    = 0;
    __cccc_tls_task                  = 0;
    t->fn(t->env);
    __cccc_tls_team   = saved_team;
    __cccc_tls_tid    = saved_tid;
    __cccc_tls_single = saved_single;
    __cccc_tls_ordered_base = saved_obase;
    __cccc_tls_ordered_from = saved_ofrom;
    __cccc_tls_dyn_base     = saved_dbase;
    __cccc_tls_da_seq       = saved_dseq;
    __cccc_tls_da           = saved_da;
    __cccc_tls_task         = saved_task;
}

static void *__cccc_pool_worker(void *arg) {
    int      id   = (int)(long)arg;
    unsigned seen = 0;
    for (;;) {
        pthread_mutex_lock(&__cccc_pool.m);
        while (__cccc_pool.gen == seen)
            pthread_cond_wait(&__cccc_pool.wake, &__cccc_pool.m);
        seen                  = __cccc_pool.gen;
        struct __cccc_team *t = __cccc_pool.team;
        int                 n = __cccc_pool.n;
        pthread_mutex_unlock(&__cccc_pool.m);
        // A worker outside the team is not waited for, so t may be gone.
        if (id < n) {
            __cccc_team_run(t, id);
            pthread_mutex_lock(&__cccc_pool.m);
            if (--__cccc_pool.pending == 0)
                pthread_cond_signal(&__cccc_pool.done);
            pthread_mutex_unlock(&__cccc_pool.m);
        }
    }
    return 0;
}

void __cccc_pool_run(void (*fn)(void *), void *env, int n) {
    if (n <= 0)
        n = __cccc_pool_default_threads();
    if (__cccc_tls_team)
        n = 1;
    struct __cccc_team t;
    t.n           = n;
    t.single_done = 0;
    t.copyprivate = 0;
    t.ordered     = 0;
    t.dyn_next    = 0;
    t.da          = 0;
    pthread_mutex_init(&t.om, 0);
    pthread_cond_init(&t.oc, 0);
    pthread_mutex_init(&t.tm, 0);
    pthread_cond_init(&t.tc, 0);
    t.hq = t.hqt = 0;
    t.rt         = 0;
    t.rt_free    = 0;
    t.live = t.nhigh = t.sleepers = t.nwait = t.starved = t.barrived = 0;
    t.bgen        = 0;
    t.fn          = fn;
    t.env         = env;
    __cccc_barrier_init(&t.bar, n);
    if (n > 1) {
        pthread_mutex_lock(&__cccc_pool.m);
        while (__cccc_pool.started < n - 1) {
            pthread_t th;
            pthread_create(&th, 0, __cccc_pool_worker,
                           (void *)(long)(__cccc_pool.started + 1));
            pthread_detach(th);
            __cccc_pool.started++;
        }
        __cccc_pool.team    = &t;
        __cccc_pool.n       = n;
        __cccc_pool.pending = n - 1;
        __cccc_pool.gen++;
        pthread_cond_broadcast(&__cccc_pool.wake);
        pthread_mutex_unlock(&__cccc_pool.m);
    }
    __cccc_team_run(&t, 0);
    if (n > 1) {
        pthread_mutex_lock(&__cccc_pool.m);
        while (__cccc_pool.pending)
            pthread_cond_wait(&__cccc_pool.done, &__cccc_pool.m);
        pthread_mutex_unlock(&__cccc_pool.m);
    }
    if (t.rt_free)
        t.rt_free(&t);
    __cccc_barrier_destroy(&t.bar);
    pthread_mutex_destroy(&t.om);
    pthread_cond_destroy(&t.oc);
    pthread_mutex_destroy(&t.tm);
    pthread_cond_destroy(&t.tc);
}
