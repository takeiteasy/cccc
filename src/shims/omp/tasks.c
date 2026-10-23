// Deferred tasks. Every thread of a team owns a Chase-Lev deque: the owner
// pushes and takes the newest task, idle threads steal the oldest. A thread
// waiting on its own tasks (taskwait, the end of a taskgroup) is tied: it runs
// only the tasks above the deque position its current task started at, and
// never steals. One mutex per team (tm) guards the dependence graph, the
// priority list, the barrier and the idle threads' sleep; the rest is atomic.
// A team of one thread, or a final task, runs each task where it is created.
//
// flags: 1 undeferred (if(false)), 2 final, 4 high priority.
enum {
    __CCCC_TASK_RING0     = 64,  // first ring size, doubles when full
    __CCCC_TASK_CAP0      = 8,   // queued tasks per thread before spawns run in place
    __CCCC_TASK_CAP_MIN   = 4,
    __CCCC_TASK_CAP_MAX   = 1024,
    __CCCC_TASK_SHRINK_AT = 64   // spawns at the cap with nothing stolen
};

struct __cccc_dep_entry {
    struct __cccc_dep_entry *next;
    void                    *addr;
    struct __cccc_task      *writer;
    struct __cccc_task     **readers;
    int                      nreaders, creaders;
};

struct __cccc_taskgroup {
    struct __cccc_taskgroup *parent;
    int                      count;
};

struct __cccc_task {
    struct __cccc_task      *next; // priority list
    void                   (*fn)(void *);
    void                    *env, *data;
    struct __cccc_task      *parent;
    struct __cccc_taskgroup *group;
    struct __cccc_dep_entry *deps; // addresses this task's children depend on
    struct __cccc_task     **succ;
    long                     mark; // own deque position when the task started
    int                      nsucc, csucc, npred, children, refs;
    unsigned char            final, high, done, undeferred, kids, tracked;
};

struct __cccc_ring {
    long                cap; // a power of two
    struct __cccc_ring *prev; // the ring this one replaced, freed with the team
    struct __cccc_task *buf[];
};

struct __cccc_deque {
    long top __attribute__((aligned(64)));    // thieves
    long bottom __attribute__((aligned(64))); // owner
    struct __cccc_ring *ring;
    long                cap, full, seen_top; // owner only: adaptive bound
    unsigned            rng;
} __attribute__((aligned(64)));

// A thread that waits for a counter sleeps here.
struct __cccc_slot {
    pthread_mutex_t m;
    pthread_cond_t  c;
    int            *watch;
    int             kick;
} __attribute__((aligned(64)));

struct __cccc_taskrt {
    struct __cccc_deque *dq;
    struct __cccc_slot  *slots;
};

static __thread struct {
    void  *data;
    int    flags, ndeps;
    void **deps;
} __cccc_tls_stage;

void *__cccc_omp_task_alloc(long n) {
    void *p = malloc(n > 0 ? (size_t)n : 1);
    if (!p)
        __builtin_trap();
    return p;
}

// The payload, deferred-ness and dependences of the task spawned next. The
// compiler emits the call right before the spawn, so nothing runs in between.
void __cccc_omp_task_stage(void *data, int flags, void **deps, int ndeps) {
    __cccc_tls_stage.data  = data;
    __cccc_tls_stage.flags = flags;
    __cccc_tls_stage.deps  = deps;
    __cccc_tls_stage.ndeps = ndeps;
}

void *__cccc_omp_task_data(void) { return __cccc_tls_task->data; }

int __cccc_omp_in_final(void) {
    return __cccc_tls_task && __cccc_tls_task->final;
}

static void __cccc_rt_free(struct __cccc_team *t) {
    struct __cccc_taskrt *rt = t->rt;
    for (int i = 0; i < t->n; i++) {
        struct __cccc_ring *r = rt->dq[i].ring;
        while (r) {
            struct __cccc_ring *prev = r->prev;
            free(r);
            r = prev;
        }
        pthread_mutex_destroy(&rt->slots[i].m);
        pthread_cond_destroy(&rt->slots[i].c);
    }
    free(rt->dq);
    free(rt->slots);
    free(rt);
    t->rt = 0;
}

// The first deferred task of a team makes its deques and slots.
static struct __cccc_taskrt *__cccc_rt(struct __cccc_team *t) {
    struct __cccc_taskrt *rt = __atomic_load_n(&t->rt, __ATOMIC_ACQUIRE);
    if (rt)
        return rt;
    pthread_mutex_lock(&t->tm);
    rt = t->rt;
    if (!rt) {
        rt = calloc(1, sizeof(*rt));
        if (!rt)
            __builtin_trap();
        rt->dq    = aligned_alloc(64, t->n * sizeof(*rt->dq));
        rt->slots = aligned_alloc(64, t->n * sizeof(*rt->slots));
        if (!rt->dq || !rt->slots)
            __builtin_trap();
        memset(rt->dq, 0, t->n * sizeof(*rt->dq));
        memset(rt->slots, 0, t->n * sizeof(*rt->slots));
        for (int i = 0; i < t->n; i++) {
            rt->dq[i].cap = __CCCC_TASK_CAP0;
            rt->dq[i].rng = 2654435761u * (unsigned)(i + 1);
            pthread_mutex_init(&rt->slots[i].m, 0);
            pthread_cond_init(&rt->slots[i].c, 0);
        }
        t->rt_free = __cccc_rt_free;
        __atomic_store_n(&t->rt, rt, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&t->tm);
    return rt;
}

static struct __cccc_ring *__cccc_dq_grow(struct __cccc_deque *q,
                                          struct __cccc_ring *old, long top,
                                          long bottom) {
    long                cap = old ? old->cap * 2 : __CCCC_TASK_RING0;
    struct __cccc_ring *r   = malloc(sizeof(*r) + cap * sizeof(r->buf[0]));
    if (!r)
        __builtin_trap();
    r->cap  = cap;
    r->prev = old;
    for (long i = top; i < bottom; i++)
        r->buf[i & (cap - 1)] = old->buf[i & (old->cap - 1)];
    __atomic_store_n(&q->ring, r, __ATOMIC_RELEASE);
    return r;
}

static void __cccc_dq_push(struct __cccc_deque *q, struct __cccc_task *tk) {
    long                b   = __atomic_load_n(&q->bottom, __ATOMIC_RELAXED);
    long                top = __atomic_load_n(&q->top, __ATOMIC_ACQUIRE);
    struct __cccc_ring *r   = q->ring;
    if (!r || b - top > r->cap - 1)
        r = __cccc_dq_grow(q, r, top, b);
    __atomic_store_n(&r->buf[b & (r->cap - 1)], tk, __ATOMIC_RELEASE);
    __atomic_store_n(&q->bottom, b + 1, __ATOMIC_RELEASE);
}

// The newest task at or above mark. The race for the last one is settled by a
// compare-and-swap on top.
static struct __cccc_task *__cccc_dq_take(struct __cccc_deque *q, long mark) {
    long b = __atomic_load_n(&q->bottom, __ATOMIC_RELAXED) - 1;
    if (b < mark)
        return 0;
    struct __cccc_ring *r = q->ring;
    __atomic_store_n(&q->bottom, b, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    long                top = __atomic_load_n(&q->top, __ATOMIC_RELAXED);
    struct __cccc_task *x   = 0;
    if (top <= b) {
        x = r->buf[b & (r->cap - 1)];
        if (top == b) {
            if (!__atomic_compare_exchange_n(&q->top, &top, top + 1, 0,
                                             __ATOMIC_SEQ_CST,
                                             __ATOMIC_RELAXED))
                x = 0;
            __atomic_store_n(&q->bottom, b + 1, __ATOMIC_RELAXED);
        }
    } else
        __atomic_store_n(&q->bottom, b + 1, __ATOMIC_RELAXED);
    return x;
}

static struct __cccc_task *__cccc_dq_steal(struct __cccc_deque *q) {
    for (;;) {
        long top = __atomic_load_n(&q->top, __ATOMIC_ACQUIRE);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        long b = __atomic_load_n(&q->bottom, __ATOMIC_ACQUIRE);
        if (top >= b)
            return 0;
        struct __cccc_ring *r = __atomic_load_n(&q->ring, __ATOMIC_ACQUIRE);
        struct __cccc_task *x =
            __atomic_load_n(&r->buf[top & (r->cap - 1)], __ATOMIC_ACQUIRE);
        if (__atomic_compare_exchange_n(&q->top, &top, top + 1, 0,
                                        __ATOMIC_SEQ_CST, __ATOMIC_RELAXED))
            return x;
    }
}

static void __cccc_task_unref(struct __cccc_task *tk);

static void __cccc_task_free_deps(struct __cccc_task *tk) {
    struct __cccc_dep_entry *e = tk->deps;
    tk->deps                   = 0;
    while (e) {
        struct __cccc_dep_entry *next = e->next;
        if (e->writer)
            __cccc_task_unref(e->writer);
        for (int i = 0; i < e->nreaders; i++)
            __cccc_task_unref(e->readers[i]);
        free(e->readers);
        free(e);
        e = next;
    }
}

// References: the task running, each child (on its parent), and each
// dependence entry that names it.
static void __cccc_task_unref(struct __cccc_task *tk) {
    if (__atomic_sub_fetch(&tk->refs, 1, __ATOMIC_ACQ_REL))
        return;
    __cccc_task_free_deps(tk);
    free(tk->succ);
    free(tk);
}

// The calling thread's task, which is the implicit task of its region until it
// has created one.
static struct __cccc_task *__cccc_task_current(void) {
    if (!__cccc_tls_task) {
        struct __cccc_task *tk = calloc(1, sizeof(*tk));
        if (!tk)
            __builtin_trap();
        tk->refs        = 1 << 30;
        __cccc_tls_task = tk;
    }
    return __cccc_tls_task;
}

// A sleeping thread is woken by the thread that makes its counter zero. The
// waiter announces itself (nwait, watch) before it re-reads the counter, and
// the finisher changes the counter before it reads them.
static void __cccc_task_notify(struct __cccc_team *t, int *counter) {
    if (!__atomic_load_n(&t->nwait, __ATOMIC_SEQ_CST))
        return;
    struct __cccc_slot *s = t->rt->slots;
    for (int i = 0; i < t->n; i++)
        if (__atomic_load_n(&s[i].watch, __ATOMIC_SEQ_CST) == counter) {
            pthread_mutex_lock(&s[i].m);
            pthread_cond_signal(&s[i].c);
            pthread_mutex_unlock(&s[i].m);
        }
}

// A priority task arrived: every waiter looks at the priority list again.
static void __cccc_task_kick(struct __cccc_team *t) {
    struct __cccc_slot *s = t->rt->slots;
    for (int i = 0; i < t->n; i++) {
        pthread_mutex_lock(&s[i].m);
        s[i].kick = 1;
        pthread_cond_signal(&s[i].c);
        pthread_mutex_unlock(&s[i].m);
    }
}

// Idle threads sleep on tc after announcing themselves in sleepers and
// looking for work once more; a task source wakes one when it makes work.
static void __cccc_task_wake_idle(struct __cccc_team *t, int locked) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (!__atomic_load_n(&t->sleepers, __ATOMIC_SEQ_CST))
        return;
    if (!locked)
        pthread_mutex_lock(&t->tm);
    pthread_cond_signal(&t->tc);
    if (!locked)
        pthread_mutex_unlock(&t->tm);
}

// Takes the oldest priority task that matches; with no parent or group, any.
// Callers hold tm.
static struct __cccc_task *__cccc_hq_pop(struct __cccc_team *t,
                                         struct __cccc_task *parent,
                                         struct __cccc_taskgroup *group) {
    int                 any  = !parent && !group;
    struct __cccc_task *prev = 0;
    for (struct __cccc_task **pp = &t->hq; *pp; prev = *pp, pp = &(*pp)->next) {
        struct __cccc_task *tk = *pp;
        if (any || (parent && tk->parent == parent) ||
            (group && tk->group == group)) {
            *pp = tk->next;
            if (t->hqt == tk)
                t->hqt = prev;
            tk->next = 0;
            __atomic_sub_fetch(&t->nhigh, 1, __ATOMIC_RELAXED);
            return tk;
        }
    }
    return 0;
}

static void __cccc_task_push(struct __cccc_team *t, struct __cccc_taskrt *rt,
                             struct __cccc_task *tk, int locked) {
    if (tk->high) {
        if (!locked)
            pthread_mutex_lock(&t->tm);
        tk->next = 0;
        if (t->hqt)
            t->hqt->next = tk;
        else
            t->hq = tk;
        t->hqt = tk;
        __atomic_add_fetch(&t->nhigh, 1, __ATOMIC_SEQ_CST);
        __cccc_task_kick(t);
        pthread_cond_signal(&t->tc);
        if (!locked)
            pthread_mutex_unlock(&t->tm);
        return;
    }
    __cccc_dq_push(&rt->dq[__cccc_tls_tid], tk);
    __cccc_task_wake_idle(t, locked);
}

static void __cccc_task_edge(struct __cccc_task *pred, struct __cccc_task *tk) {
    if (pred == tk || pred->done)
        return;
    if (pred->nsucc == pred->csucc) {
        pred->csucc = pred->csucc ? pred->csucc * 2 : 4;
        pred->succ  = realloc(pred->succ, pred->csucc * sizeof(*pred->succ));
        if (!pred->succ)
            __builtin_trap();
    }
    pred->succ[pred->nsucc++] = tk;
    __atomic_add_fetch(&tk->npred, 1, __ATOMIC_SEQ_CST);
}

// Each dependence is an address and a kind (0 in, else out/inout), against the
// earlier siblings that named the same address. A writer waits for the readers
// since the last writer, or for that writer when there are none. Callers hold
// tm.
static void __cccc_task_deps(struct __cccc_task *tk, void **deps, int nd) {
    struct __cccc_task *p = tk->parent;
    for (int i = 0; i < nd; i++) {
        void                    *addr  = deps[2 * i];
        int                      write = deps[2 * i + 1] != 0;
        struct __cccc_dep_entry *e     = p->deps;
        while (e && e->addr != addr)
            e = e->next;
        if (!e) {
            e = calloc(1, sizeof(*e));
            if (!e)
                __builtin_trap();
            e->addr = addr;
            e->next = p->deps;
            p->deps = e;
        }
        if (write) {
            int had_readers = e->nreaders;
            for (int j = 0; j < e->nreaders; j++) {
                __cccc_task_edge(e->readers[j], tk);
                __cccc_task_unref(e->readers[j]);
            }
            e->nreaders = 0;
            if (!had_readers && e->writer)
                __cccc_task_edge(e->writer, tk);
            if (e->writer)
                __cccc_task_unref(e->writer);
            e->writer = tk;
            __atomic_add_fetch(&tk->refs, 1, __ATOMIC_RELAXED);
        } else {
            if (e->writer)
                __cccc_task_edge(e->writer, tk);
            if (e->nreaders == e->creaders) {
                e->creaders = e->creaders ? e->creaders * 2 : 4;
                e->readers =
                    realloc(e->readers, e->creaders * sizeof(*e->readers));
                if (!e->readers)
                    __builtin_trap();
            }
            e->readers[e->nreaders++] = tk;
            __atomic_add_fetch(&tk->refs, 1, __ATOMIC_RELAXED);
        }
    }
}

// The task is over: release the tasks that waited for it, then settle the
// counts its parent, its group and the team keep. The team's live count goes
// last, because the end of a region frees the implicit tasks once it is zero.
static void __cccc_task_complete(struct __cccc_team *t, struct __cccc_taskrt *rt,
                                 struct __cccc_task *tk) {
    if (tk->tracked) {
        pthread_mutex_lock(&t->tm);
        tk->done = 1;
        for (int i = 0; i < tk->nsucc; i++) {
            struct __cccc_task *s = tk->succ[i];
            if (__atomic_sub_fetch(&s->npred, 1, __ATOMIC_SEQ_CST) == 0) {
                if (s->undeferred)
                    __cccc_task_notify(t, &s->npred);
                else
                    __cccc_task_push(t, rt, s, 1);
            }
        }
        pthread_mutex_unlock(&t->tm);
    }
    struct __cccc_task *parent = tk->parent;
    if (__atomic_sub_fetch(&parent->children, 1, __ATOMIC_SEQ_CST) == 0)
        __cccc_task_notify(t, &parent->children);
    if (tk->group &&
        __atomic_sub_fetch(&tk->group->count, 1, __ATOMIC_SEQ_CST) == 0)
        __cccc_task_notify(t, &tk->group->count);
    __cccc_task_unref(parent);
    __cccc_task_unref(tk);
    if (__atomic_sub_fetch(&t->live, 1, __ATOMIC_SEQ_CST) == 0 &&
        __atomic_load_n(&t->sleepers, __ATOMIC_SEQ_CST)) {
        pthread_mutex_lock(&t->tm);
        pthread_cond_broadcast(&t->tc);
        pthread_mutex_unlock(&t->tm);
    }
}

static void __cccc_task_run(struct __cccc_team *t, struct __cccc_task *tk) {
    struct __cccc_taskrt *rt    = t->rt;
    struct __cccc_task   *saved = __cccc_tls_task;
    tk->mark = __atomic_load_n(&rt->dq[__cccc_tls_tid].bottom, __ATOMIC_RELAXED);
    __cccc_tls_task = tk;
    tk->fn(tk->env);
    __cccc_tls_task = saved;
    free(tk->data);
    tk->data = 0;
    __cccc_task_complete(t, rt, tk);
}

// Runs the calling thread's tasks above cur's mark, and the priority tasks
// that are cur's children or in group g, until *counter is zero.
static void __cccc_task_wait(struct __cccc_team *t, struct __cccc_task *cur,
                             struct __cccc_taskgroup *g, int *counter) {
    struct __cccc_taskrt *rt = t->rt;
    int                   id = __cccc_tls_tid;
    struct __cccc_slot   *s  = &rt->slots[id];
    while (__atomic_load_n(counter, __ATOMIC_ACQUIRE) > 0) {
        struct __cccc_task *tk = __cccc_dq_take(&rt->dq[id], cur->mark);
        if (!tk && __atomic_load_n(&t->nhigh, __ATOMIC_RELAXED)) {
            pthread_mutex_lock(&t->tm);
            tk = __cccc_hq_pop(t, cur, g);
            pthread_mutex_unlock(&t->tm);
        }
        if (tk) {
            __cccc_task_run(t, tk);
            continue;
        }
        __atomic_add_fetch(&t->nwait, 1, __ATOMIC_SEQ_CST);
        pthread_mutex_lock(&s->m);
        __atomic_store_n(&s->watch, counter, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(counter, __ATOMIC_SEQ_CST) > 0 && !s->kick)
            pthread_cond_wait(&s->c, &s->m);
        s->kick = 0;
        __atomic_store_n(&s->watch, 0, __ATOMIC_SEQ_CST);
        pthread_mutex_unlock(&s->m);
        __atomic_sub_fetch(&t->nwait, 1, __ATOMIC_SEQ_CST);
    }
}

// A task that ran in place leaves with its children done, so its record can
// go. The last child may still be dropping its reference on the record.
static void __cccc_task_finish(struct __cccc_team *t, struct __cccc_task *tk) {
    __cccc_task_wait(t, tk, 0, &tk->children);
    while (__atomic_load_n(&tk->refs, __ATOMIC_ACQUIRE) != 1 << 30)
        sched_yield();
    __cccc_task_free_deps(tk);
}

// Whether a spawn runs in place: this thread has the number of tasks queued
// that its bound allows. The bound doubles when an idle thread found nothing
// to steal, and halves after a stretch of full spawns with nothing stolen.
static int __cccc_task_full(struct __cccc_team *t, struct __cccc_taskrt *rt) {
    struct __cccc_deque *q = &rt->dq[__cccc_tls_tid];
    if (__atomic_load_n(&t->starved, __ATOMIC_RELAXED) &&
        __atomic_exchange_n(&t->starved, 0, __ATOMIC_RELAXED)) {
        if (q->cap < __CCCC_TASK_CAP_MAX)
            q->cap *= 2;
        q->full = 0;
    }
    long top = __atomic_load_n(&q->top, __ATOMIC_RELAXED);
    if (q->bottom - top < q->cap)
        return 0;
    if (top != q->seen_top) {
        q->seen_top = top;
        q->full     = 0;
    } else if (++q->full >= __CCCC_TASK_SHRINK_AT &&
               q->cap > __CCCC_TASK_CAP_MIN) {
        q->cap >>= 1;
        q->full = 0;
    }
    return 1;
}

void __cccc_omp_task_spawn(void (*fn)(void *), long envsz, void *env) {
    void               *data  = __cccc_tls_stage.data;
    int                 flags = __cccc_tls_stage.flags;
    void              **deps  = __cccc_tls_stage.deps;
    int                 nd    = __cccc_tls_stage.ndeps;
    struct __cccc_team *t     = __cccc_tls_team;
    struct __cccc_task *cur   = __cccc_tls_task;

    // Runs here, on a record of the stack: no team, a final task, a task with
    // if(false) and no dependences, or a thread with its bound queued.
    struct __cccc_taskrt *rt = 0;
    if (t && t->n > 1)
        rt = __cccc_rt(t);
    if (!rt || (cur && cur->final) ||
        (!nd && ((flags & 1) || __cccc_task_full(t, rt)))) {
        struct __cccc_task tk;
        memset(&tk, 0, sizeof(tk));
        tk.refs  = 1 << 30;
        tk.final = (flags & 2) || (cur && cur->final);
        tk.data  = data;
        if (rt)
            tk.mark = __atomic_load_n(&rt->dq[__cccc_tls_tid].bottom,
                                      __ATOMIC_RELAXED);
        __cccc_tls_task = &tk;
        fn(env);
        __cccc_tls_task = cur;
        free(data);
        if (tk.kids)
            __cccc_task_finish(t, &tk);
        return;
    }

    cur = __cccc_task_current();
    size_t off = (sizeof(struct __cccc_task) + 15) & ~(size_t)15;
    struct __cccc_task *tk = calloc(1, off + (size_t)envsz);
    if (!tk)
        __builtin_trap();
    tk->env = (char *)tk + off;
    memcpy(tk->env, env, (size_t)envsz);
    tk->fn         = fn;
    tk->data       = data;
    tk->parent     = cur;
    tk->final      = (flags & 2) != 0;
    tk->high       = (flags & 4) != 0;
    tk->undeferred = (flags & 1) != 0;
    tk->tracked    = nd > 0;
    tk->refs       = 1;

    __atomic_add_fetch(&cur->refs, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&cur->children, 1, __ATOMIC_SEQ_CST);
    cur->kids = 1;
    tk->group = cur->group;
    if (tk->group)
        __atomic_add_fetch(&tk->group->count, 1, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&t->live, 1, __ATOMIC_SEQ_CST);
    if (nd) {
        pthread_mutex_lock(&t->tm);
        __cccc_task_deps(tk, deps, nd);
        if (!tk->undeferred && tk->npred == 0)
            __cccc_task_push(t, rt, tk, 1);
        pthread_mutex_unlock(&t->tm);
        if (tk->undeferred) {
            __cccc_task_wait(t, cur, 0, &tk->npred);
            __cccc_task_run(t, tk);
        }
        return;
    }
    __cccc_task_push(t, rt, tk, 0);
}

// Runs the calling thread's own children, newest first, until they are done.
void __cccc_omp_taskwait(void) {
    struct __cccc_team *t   = __cccc_tls_team;
    struct __cccc_task *cur = __cccc_tls_task;
    if (!t || t->n == 1 || !cur ||
        !__atomic_load_n(&cur->children, __ATOMIC_RELAXED))
        return;
    __cccc_task_wait(t, cur, 0, &cur->children);
}

void *__cccc_omp_taskgroup_begin(void) {
    struct __cccc_team *t = __cccc_tls_team;
    if (!t || t->n == 1)
        return 0;
    struct __cccc_task      *cur = __cccc_task_current();
    struct __cccc_taskgroup *g   = calloc(1, sizeof(*g));
    if (!g)
        __builtin_trap();
    g->parent  = cur->group;
    cur->group = g;
    return g;
}

// Waits for every task created in the group, including the descendants that
// inherited it.
void __cccc_omp_taskgroup_end(void *group) {
    struct __cccc_taskgroup *g = group;
    if (!g)
        return;
    struct __cccc_team *t   = __cccc_tls_team;
    struct __cccc_task *cur = __cccc_tls_task;
    if (__atomic_load_n(&g->count, __ATOMIC_RELAXED) > 0)
        __cccc_task_wait(t, cur, g, &g->count);
    cur->group = g->parent;
    free(g);
}

// Takes a task from the priority list, the thread's own deque, or another
// thread's deque. A thread that finds nothing tells the owners to queue more.
static struct __cccc_task *__cccc_task_find(struct __cccc_team *t,
                                            struct __cccc_taskrt *rt) {
    struct __cccc_task *tk;
    if (__atomic_load_n(&t->nhigh, __ATOMIC_RELAXED)) {
        pthread_mutex_lock(&t->tm);
        tk = __cccc_hq_pop(t, 0, 0);
        pthread_mutex_unlock(&t->tm);
        if (tk)
            return tk;
    }
    int                  id = __cccc_tls_tid;
    struct __cccc_deque *me = &rt->dq[id];
    if ((tk = __cccc_dq_take(me, 0)))
        return tk;
    me->rng ^= me->rng << 13;
    me->rng ^= me->rng >> 17;
    me->rng ^= me->rng << 5;
    for (int i = 0; i < t->n; i++) {
        int v = (int)((me->rng + (unsigned)i) % (unsigned)t->n);
        if (v != id && (tk = __cccc_dq_steal(&rt->dq[v])))
            return tk;
    }
    if (!__atomic_load_n(&t->starved, __ATOMIC_RELAXED))
        __atomic_store_n(&t->starved, 1, __ATOMIC_RELAXED);
    return 0;
}

static int __cccc_task_any(struct __cccc_team *t, struct __cccc_taskrt *rt) {
    if (__atomic_load_n(&t->nhigh, __ATOMIC_SEQ_CST))
        return 1;
    if (!rt)
        return 0;
    for (int i = 0; i < t->n; i++)
        if (__atomic_load_n(&rt->dq[i].bottom, __ATOMIC_SEQ_CST) >
            __atomic_load_n(&rt->dq[i].top, __ATOMIC_SEQ_CST))
            return 1;
    return 0;
}

// A thread that reaches the barrier runs tasks until every thread has
// arrived and no task is left.
void __cccc_omp_barrier(void) {
    struct __cccc_team *t = __cccc_tls_team;
    if (!t || t->n == 1)
        return;
    pthread_mutex_lock(&t->tm);
    unsigned gen = t->bgen;
    t->barrived++;
    for (;;) {
        if (t->barrived == t->n &&
            !__atomic_load_n(&t->live, __ATOMIC_SEQ_CST)) {
            t->barrived = 0;
            t->bgen++;
            pthread_cond_broadcast(&t->tc);
            break;
        }
        if (gen != t->bgen)
            break;
        struct __cccc_taskrt *rt = __atomic_load_n(&t->rt, __ATOMIC_ACQUIRE);
        struct __cccc_task   *tk = 0;
        if (rt) {
            pthread_mutex_unlock(&t->tm);
            tk = __cccc_task_find(t, rt);
            if (tk)
                __cccc_task_run(t, tk);
            pthread_mutex_lock(&t->tm);
            if (tk)
                continue;
        }
        __atomic_add_fetch(&t->sleepers, 1, __ATOMIC_SEQ_CST);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        rt = __atomic_load_n(&t->rt, __ATOMIC_ACQUIRE);
        if (gen == t->bgen &&
            (t->barrived != t->n ||
             __atomic_load_n(&t->live, __ATOMIC_SEQ_CST)) &&
            !__cccc_task_any(t, rt))
            pthread_cond_wait(&t->tc, &t->tm);
        __atomic_sub_fetch(&t->sleepers, 1, __ATOMIC_SEQ_CST);
    }
    pthread_mutex_unlock(&t->tm);
}

// The end of a parallel region: runs tasks until none is left, then drops the
// thread's implicit task.
void __cccc_omp_task_drain(void) {
    struct __cccc_team *t = __cccc_tls_team;
    if (t && t->n > 1) {
        while (__atomic_load_n(&t->live, __ATOMIC_SEQ_CST)) {
            struct __cccc_taskrt *rt = __atomic_load_n(&t->rt, __ATOMIC_ACQUIRE);
            struct __cccc_task   *tk = rt ? __cccc_task_find(t, rt) : 0;
            if (tk) {
                __cccc_task_run(t, tk);
                continue;
            }
            pthread_mutex_lock(&t->tm);
            __atomic_add_fetch(&t->sleepers, 1, __ATOMIC_SEQ_CST);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            rt = __atomic_load_n(&t->rt, __ATOMIC_ACQUIRE);
            if (__atomic_load_n(&t->live, __ATOMIC_SEQ_CST) &&
                !__cccc_task_any(t, rt))
                pthread_cond_wait(&t->tc, &t->tm);
            __atomic_sub_fetch(&t->sleepers, 1, __ATOMIC_SEQ_CST);
            pthread_mutex_unlock(&t->tm);
        }
    }
    if (__cccc_tls_task) {
        __cccc_task_free_deps(__cccc_tls_task);
        free(__cccc_tls_task);
        __cccc_tls_task = 0;
    }
}
