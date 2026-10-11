void __cccc_omp_barrier(void) {
    if (__cccc_tls_team && __cccc_tls_team->n > 1)
        __cccc_barrier_wait(&__cccc_tls_team->bar);
}

int __cccc_omp_thread_num(void) { return __cccc_tls_tid; }

int __cccc_omp_num_threads(void) {
    return __cccc_tls_team ? __cccc_tls_team->n : 1;
}

void __cccc_omp_static_range(long total, long *begin, long *end) {
    long n = __cccc_omp_num_threads(), t = __cccc_omp_thread_num();
    long q = total / n, r = total % n;
    *begin = t * q + (t < r ? t : r);
    *end   = *begin + q + (t < r ? 1 : 0);
}

int __cccc_omp_single_begin(void) {
    struct __cccc_team *t = __cccc_tls_team;
    if (!t || t->n == 1)
        return 1;
    int seq = ++__cccc_tls_single;
    int cur = __atomic_load_n(&t->single_done, __ATOMIC_ACQUIRE);
    while (cur < seq)
        if (__atomic_compare_exchange_n(&t->single_done, &cur, seq, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return 1;
    return 0;
}

static void *__cccc_omp_cp_solo;

void __cccc_omp_copyprivate_set(void *slots) {
    if (__cccc_tls_team)
        __cccc_tls_team->copyprivate = slots;
    else
        __cccc_omp_cp_solo = slots;
}

void *__cccc_omp_copyprivate_get(void) {
    return __cccc_tls_team ? __cccc_tls_team->copyprivate : __cccc_omp_cp_solo;
}

static pthread_mutex_t __cccc_omp_reduce_mutex = PTHREAD_MUTEX_INITIALIZER;

void __cccc_omp_reduce_lock(void) { pthread_mutex_lock(&__cccc_omp_reduce_mutex); }

void __cccc_omp_reduce_unlock(void) {
    pthread_mutex_unlock(&__cccc_omp_reduce_mutex);
}

static struct {
    const char     *name;
    pthread_mutex_t m;
} __cccc_omp_crit[64];
static int             __cccc_omp_crit_n;
static pthread_mutex_t __cccc_omp_crit_m = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t *__cccc_omp_crit_find(const char *name) {
    pthread_mutex_lock(&__cccc_omp_crit_m);
    int i = 0;
    while (i < __cccc_omp_crit_n && strcmp(__cccc_omp_crit[i].name, name))
        i++;
    if (i == __cccc_omp_crit_n) {
        if (i == 64)
            __builtin_trap();
        __cccc_omp_crit[i].name = name;
        pthread_mutex_init(&__cccc_omp_crit[i].m, 0);
        __cccc_omp_crit_n++;
    }
    pthread_mutex_unlock(&__cccc_omp_crit_m);
    return &__cccc_omp_crit[i].m;
}

void __cccc_omp_critical_enter(const char *name) {
    pthread_mutex_lock(__cccc_omp_crit_find(name));
}

void __cccc_omp_critical_exit(const char *name) {
    pthread_mutex_unlock(__cccc_omp_crit_find(name));
}
