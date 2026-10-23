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

// Schedule kinds follow omp_sched_t; 0x10 is `runtime`. A loop calls
// loop_begin once, then loop_next until it returns 0 for [b, e) chunks of the
// n iterations. Dynamic and guided loops claim from one counter that only
// grows; each thread adds the trip count of every such loop to its base, so
// the counter needs no reset between loops and a nowait loop can be entered
// while a slower thread is still claiming from the one before.
static int  __cccc_omp_sched_kind = 1;
static long __cccc_omp_sched_chunk;
static pthread_once_t __cccc_omp_sched_once = PTHREAD_ONCE_INIT;

static int __cccc_omp_sched_word(const char **p, const char *w) {
    const char *s = *p;
    for (; *w; s++, w++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != *w)
            return 0;
    }
    *p = s;
    return 1;
}

// OMP_SCHEDULE, e.g. "dynamic,4" or "nonmonotonic:guided"; an unparsable value
// leaves the default.
static void __cccc_omp_sched_init(void) {
    const char *s = getenv("OMP_SCHEDULE");
    if (!s)
        return;
    while (*s == ' ')
        s++;
    if (!__cccc_omp_sched_word(&s, "monotonic:"))
        __cccc_omp_sched_word(&s, "nonmonotonic:");
    int k;
    if (__cccc_omp_sched_word(&s, "static"))
        k = 1;
    else if (__cccc_omp_sched_word(&s, "dynamic"))
        k = 2;
    else if (__cccc_omp_sched_word(&s, "guided"))
        k = 3;
    else if (__cccc_omp_sched_word(&s, "auto"))
        k = 4;
    else
        return;
    long c = 0;
    while (*s == ' ')
        s++;
    if (*s == ',') {
        char *end;
        c = strtol(s + 1, &end, 10);
        if (end == s + 1 || c <= 0)
            return;
    }
    __cccc_omp_sched_kind  = k;
    __cccc_omp_sched_chunk = c;
}

void __cccc_omp_sched_set(int kind, int chunk) {
    pthread_once(&__cccc_omp_sched_once, __cccc_omp_sched_init);
    kind &= 0x7fffffff;
    if (kind < 1 || kind > 4)
        return;
    __cccc_omp_sched_kind  = kind;
    __cccc_omp_sched_chunk = chunk;
}

void __cccc_omp_sched_get(int *kind, int *chunk) {
    pthread_once(&__cccc_omp_sched_once, __cccc_omp_sched_init);
    *kind  = __cccc_omp_sched_kind;
    *chunk = (int)__cccc_omp_sched_chunk;
}

void __cccc_omp_loop_begin(int *kind, long *chunk) {
    int k = *kind & 0x7fffffff;
    if (k == 0x10) {
        pthread_once(&__cccc_omp_sched_once, __cccc_omp_sched_init);
        k      = __cccc_omp_sched_kind;
        *chunk = __cccc_omp_sched_chunk;
    }
    if (k == 4) {
        k      = 1;
        *chunk = 0;
    }
    if (*chunk < 0 || (*chunk == 0 && k != 1))
        *chunk = k == 1 ? 0 : 1;
    *kind = k;
}

int __cccc_omp_loop_next(int kind, long chunk, long n, long *st, long *b,
                         long *e) {
    struct __cccc_team *t = __cccc_tls_team;
    long                nt = t ? t->n : 1;
    if (nt == 1) {
        if (*st)
            return 0;
        *st = 1;
        *b  = 0;
        *e  = n;
        return 1;
    }
    if (kind == 1) {
        if (chunk == 0) {
            if (*st)
                return 0;
            *st = 1;
            __cccc_omp_static_range(n, b, e);
            return 1;
        }
        long start = (__cccc_tls_tid + *st * nt) * chunk;
        if (start >= n)
            return 0;
        (*st)++;
        *b = start;
        *e = n - start > chunk ? start + chunk : n;
        return 1;
    }
    long lo = __cccc_tls_dyn_base, hi = lo + n;
    long cur = __atomic_load_n(&t->dyn_next, __ATOMIC_ACQUIRE);
    for (;;) {
        if (cur >= hi)
            return 0;
        if (cur < lo)
            __builtin_trap();
        long size = chunk;
        if (kind == 3) {
            long g = (hi - cur + nt - 1) / nt;
            if (g > size)
                size = g;
        }
        long next = size > hi - cur ? hi : cur + size;
        if (__atomic_compare_exchange_n(&t->dyn_next, &cur, next, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            *b = cur - lo;
            *e = next - lo;
            return 1;
        }
    }
}

void __cccc_omp_loop_end(int kind, long n) {
    if (kind >= 2)
        __cccc_tls_dyn_base += n;
}

// Ordered regions run in iteration order by passing a baton between chunks: a
// thread waits for every earlier iteration before its first ordered region and
// hands over at the end of each chunk. Chunks are claimed in increasing order,
// so the thread holding the lowest unfinished chunk never waits. `ordered` only
// grows; each thread adds the loop's trip count to its base so later loops need
// no reset.
void __cccc_omp_ordered_start(long b) {
    __cccc_tls_ordered_from = __cccc_tls_ordered_base + b;
}

static void __cccc_omp_ordered_wait(struct __cccc_team *t) {
    pthread_mutex_lock(&t->om);
    while (t->ordered < __cccc_tls_ordered_from)
        pthread_cond_wait(&t->oc, &t->om);
    pthread_mutex_unlock(&t->om);
}

void __cccc_omp_ordered_begin(void) {
    struct __cccc_team *t = __cccc_tls_team;
    if (!t || t->n == 1 || __cccc_tls_ordered_from < 0)
        return;
    __cccc_omp_ordered_wait(t);
}

void __cccc_omp_ordered_chunk_end(long e) {
    struct __cccc_team *t = __cccc_tls_team;
    if (t && t->n > 1 && __cccc_tls_ordered_from >= 0) {
        long start = __cccc_tls_ordered_from;
        long end   = __cccc_tls_ordered_base + e;
        if (end > start) {
            __cccc_omp_ordered_wait(t);
            pthread_mutex_lock(&t->om);
            t->ordered = end;
            pthread_cond_broadcast(&t->oc);
            pthread_mutex_unlock(&t->om);
        }
    }
    __cccc_tls_ordered_from = -1;
}

void __cccc_omp_ordered_loop_end(long n) {
    __cccc_tls_ordered_base += n;
    __cccc_tls_ordered_from = -1;
}

// Doacross loops (`ordered(n)` with sink/source). A loop's iterations are
// numbered in one flat space, `flat = outer * inner + position inside the
// outer iteration`, where `outer` is the collapsed index the schedule hands
// out. Chunks run their iterations in order on one thread, so one counter per
// chunk is enough: it holds the last flat position posted plus one. Loop state
// lives in a list on the team, found by a per-thread sequence number, and is
// freed by the last thread to leave the loop.
struct __cccc_doacross {
    struct __cccc_doacross *next;
    long                    seq, n, inner, chunk, nent;
    int                     kind, nt, left;
    long                   *ent;
};

void __cccc_omp_doacross_init(long n, long inner, int kind, long chunk) {
    struct __cccc_team *t = __cccc_tls_team;
    __cccc_tls_da         = 0;
    if (!t || t->n == 1)
        return;
    long seq = ++__cccc_tls_da_seq;
    pthread_mutex_lock(&t->om);
    struct __cccc_doacross *d = t->da;
    while (d && d->seq != seq)
        d = d->next;
    if (!d) {
        d = calloc(1, sizeof(*d));
        if (!d)
            __builtin_trap();
        d->seq   = seq;
        d->n     = n;
        d->inner = inner > 0 ? inner : 1;
        d->kind  = kind;
        d->chunk = chunk;
        d->nt    = t->n;
        d->left  = t->n;
        // TODO: guided loops keep a counter per outer iteration, so memory
        // grows with the trip count (#1751).
        d->nent = kind == 3 ? n : (kind == 1 && !chunk) ? t->n : (n + chunk - 1) / chunk;
        d->ent  = calloc(d->nent > 0 ? d->nent : 1, sizeof(long));
        if (!d->ent)
            __builtin_trap();
        d->next = t->da;
        t->da   = d;
    }
    pthread_mutex_unlock(&t->om);
    __cccc_tls_da = d;
}

void __cccc_omp_doacross_fini(void) {
    struct __cccc_team *t = __cccc_tls_team;
    struct __cccc_doacross *d = __cccc_tls_da;
    __cccc_tls_da         = 0;
    if (!d)
        return;
    pthread_mutex_lock(&t->om);
    if (--d->left == 0) {
        struct __cccc_doacross **p = &t->da;
        while (*p != d)
            p = &(*p)->next;
        *p = d->next;
        free(d->ent);
        free(d);
    }
    pthread_mutex_unlock(&t->om);
}

static long *__cccc_omp_doacross_slot(struct __cccc_doacross *d, long flat) {
    long k = flat / d->inner, e;
    if (d->kind == 3)
        e = k;
    else if (d->chunk)
        e = k / d->chunk;
    else {
        long q = d->n / d->nt, r = d->n % d->nt, cut = r * (q + 1);
        e = k < cut ? k / (q + 1) : r + (k - cut) / q;
    }
    return &d->ent[e];
}

void __cccc_omp_doacross_post(long flat) {
    struct __cccc_doacross *d = __cccc_tls_da;
    if (!d)
        return;
    long *slot = __cccc_omp_doacross_slot(d, flat);
    if (*slot < flat + 1)
        __atomic_store_n(slot, flat + 1, __ATOMIC_RELEASE);
}

// Posts the end of outer iteration k, so a `continue` past the source (or a
// body without one) never leaves a waiter behind.
void __cccc_omp_doacross_done(long k) {
    struct __cccc_doacross *d = __cccc_tls_da;
    if (d)
        __cccc_omp_doacross_post((k + 1) * d->inner - 1);
}

// TODO: the wait spins, which wastes CPU while a thread is far behind
// (#1752).
void __cccc_omp_doacross_wait(long flat) {
    struct __cccc_doacross *d = __cccc_tls_da;
    if (!d)
        return;
    long *slot = __cccc_omp_doacross_slot(d, flat);
    while (__atomic_load_n(slot, __ATOMIC_ACQUIRE) <= flat)
        sched_yield();
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

// Named critical sections: one mutex per name, found through a hash table on
// the first entry from each call site, which caches the mutex. Mutexes are
// allocated apart from the slots, so a pointer stays valid across a rehash.
struct __cccc_crit_slot {
    const char      *name;
    pthread_mutex_t *m;
};

static struct __cccc_crit_slot *__cccc_omp_crit_tab;
static size_t                   __cccc_omp_crit_cap, __cccc_omp_crit_n;
static pthread_mutex_t          __cccc_omp_crit_m = PTHREAD_MUTEX_INITIALIZER;

static size_t __cccc_omp_crit_hash(const char *s) {
    size_t h = (size_t)14695981039346656037ull;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * (size_t)1099511628211ull;
    return h;
}

static struct __cccc_crit_slot *__cccc_omp_crit_probe(
    struct __cccc_crit_slot *tab, size_t cap, const char *name) {
    size_t i = __cccc_omp_crit_hash(name) & (cap - 1);
    while (tab[i].name && strcmp(tab[i].name, name))
        i = (i + 1) & (cap - 1);
    return &tab[i];
}

static pthread_mutex_t *__cccc_omp_crit_find(const char *name) {
    pthread_mutex_lock(&__cccc_omp_crit_m);
    if ((__cccc_omp_crit_n + 1) * 2 > __cccc_omp_crit_cap) {
        size_t cap = __cccc_omp_crit_cap ? __cccc_omp_crit_cap * 2 : 16;
        struct __cccc_crit_slot *tab = calloc(cap, sizeof(*tab));
        if (!tab)
            __builtin_trap();
        for (size_t i = 0; i < __cccc_omp_crit_cap; i++)
            if (__cccc_omp_crit_tab[i].name)
                *__cccc_omp_crit_probe(tab, cap,
                                       __cccc_omp_crit_tab[i].name) =
                    __cccc_omp_crit_tab[i];
        free(__cccc_omp_crit_tab);
        __cccc_omp_crit_tab = tab;
        __cccc_omp_crit_cap = cap;
    }
    struct __cccc_crit_slot *s =
        __cccc_omp_crit_probe(__cccc_omp_crit_tab, __cccc_omp_crit_cap, name);
    if (!s->name) {
        s->m = malloc(sizeof(*s->m));
        if (!s->m)
            __builtin_trap();
        pthread_mutex_init(s->m, 0);
        s->name = name;
        __cccc_omp_crit_n++;
    }
    pthread_mutex_t *m = s->m;
    pthread_mutex_unlock(&__cccc_omp_crit_m);
    return m;
}

void *__cccc_omp_critical_enter(void **cache, const char *name) {
    pthread_mutex_t *m = __atomic_load_n(cache, __ATOMIC_ACQUIRE);
    if (!m) {
        m = __cccc_omp_crit_find(name);
        __atomic_store_n(cache, m, __ATOMIC_RELEASE);
    }
    pthread_mutex_lock(m);
    return m;
}

void __cccc_omp_critical_exit(void *m) { pthread_mutex_unlock(m); }
