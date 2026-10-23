// Deferred tasks. One mutex per team guards the queues, the counters and every
// task's bookkeeping; task bodies run unlocked. A team of one thread, or a
// final task, runs each task at the point it is created.
//
// flags: 1 undeferred (if(false)), 2 final, 4 high priority.
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
    struct __cccc_task      *next;
    void                   (*fn)(void *);
    void                    *env, *data;
    struct __cccc_task      *parent;
    struct __cccc_taskgroup *group;
    struct __cccc_dep_entry *deps; // addresses this task's children depend on
    struct __cccc_task     **succ;
    int                      nsucc, csucc, npred, children, refs;
    unsigned char            final, high, done, undeferred, kids;
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
// dependence entry that names it. Callers hold the team mutex.
static void __cccc_task_unref(struct __cccc_task *tk) {
    if (--tk->refs)
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

// Every wait is on one condition variable; the wake-up is skipped while no
// thread is waiting.
static void __cccc_task_wait(struct __cccc_team *t) {
    t->waiting++;
    pthread_cond_wait(&t->tc, &t->tm);
    t->waiting--;
}

static void __cccc_task_wake(struct __cccc_team *t) {
    if (t->waiting)
        pthread_cond_broadcast(&t->tc);
}

static void __cccc_task_push(struct __cccc_team *t, struct __cccc_task *tk) {
    struct __cccc_task **head = tk->high ? &t->hq : &t->lq;
    struct __cccc_task **tail = tk->high ? &t->hqt : &t->lqt;
    tk->next                  = 0;
    if (*tail)
        (*tail)->next = tk;
    else
        *head = tk;
    *tail = tk;
    __atomic_add_fetch(&t->queued, 1, __ATOMIC_RELAXED);
    __cccc_task_wake(t);
}

// Idle threads take the oldest task, which is the largest one; a thread
// waiting on its own children takes the newest child or group member.
static struct __cccc_task *__cccc_task_pop(struct __cccc_team *t,
                                           struct __cccc_task *parent,
                                           struct __cccc_taskgroup *group) {
    int any = !parent && !group;
    for (int pass = 0; pass < 2; pass++) {
        struct __cccc_task **head = pass ? &t->lq : &t->hq;
        struct __cccc_task **tail = pass ? &t->lqt : &t->hqt;
        struct __cccc_task **hit = 0, *hit_prev = 0, *prev = 0;
        for (struct __cccc_task **pp = head; *pp; prev = *pp, pp = &(*pp)->next) {
            struct __cccc_task *tk = *pp;
            if (any || (parent && tk->parent == parent) ||
                (group && tk->group == group)) {
                hit      = pp;
                hit_prev = prev;
                if (any)
                    break;
            }
        }
        if (hit) {
            struct __cccc_task *tk = *hit;
            *hit                   = tk->next;
            if (*tail == tk)
                *tail = hit_prev;
            __atomic_sub_fetch(&t->queued, 1, __ATOMIC_RELAXED);
            return tk;
        }
    }
    return 0;
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
    tk->npred++;
}

// Each dependence is an address and a kind (0 in, else out/inout), against the
// earlier siblings that named the same address. A writer waits for the readers
// since the last writer, or for that writer when there are none.
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
            tk->refs++;
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
            tk->refs++;
        }
    }
}

static void __cccc_task_run(struct __cccc_team *t, struct __cccc_task *tk) {
    struct __cccc_task *saved = __cccc_tls_task;
    __cccc_tls_task           = tk;
    tk->fn(tk->env);
    __cccc_tls_task = saved;
    free(tk->data);
    tk->data = 0;
    pthread_mutex_lock(&t->tm);
    tk->done = 1;
    for (int i = 0; i < tk->nsucc; i++)
        if (--tk->succ[i]->npred == 0 && !tk->succ[i]->undeferred)
            __cccc_task_push(t, tk->succ[i]);
    __atomic_sub_fetch(&tk->parent->children, 1, __ATOMIC_RELAXED);
    if (tk->group)
        tk->group->count--;
    t->live--;
    __cccc_task_wake(t);
    __cccc_task_unref(tk->parent);
    __cccc_task_unref(tk);
    pthread_mutex_unlock(&t->tm);
}

// A task that ran in place leaves with its children done, so its record can
// go.
static void __cccc_task_finish(struct __cccc_team *t, struct __cccc_task *tk) {
    pthread_mutex_lock(&t->tm);
    while (tk->children > 0) {
        struct __cccc_task *child = __cccc_task_pop(t, tk, 0);
        if (child) {
            pthread_mutex_unlock(&t->tm);
            __cccc_task_run(t, child);
            pthread_mutex_lock(&t->tm);
        } else
            __cccc_task_wait(t);
    }
    __cccc_task_free_deps(tk);
    pthread_mutex_unlock(&t->tm);
}

void __cccc_omp_task_spawn(void (*fn)(void *), long envsz, void *env) {
    void               *data  = __cccc_tls_stage.data;
    int                 flags = __cccc_tls_stage.flags;
    void              **deps  = __cccc_tls_stage.deps;
    int                 nd    = __cccc_tls_stage.ndeps;
    struct __cccc_team *t     = __cccc_tls_team;
    struct __cccc_task *cur   = __cccc_tls_task;

    // Runs here, on a record of the stack: no team, a final task, a task with
    // if(false) and no dependences, or a full queue.
    if (!t || t->n == 1 || (cur && cur->final) ||
        (!nd && ((flags & 1) ||
                 __atomic_load_n(&t->queued, __ATOMIC_RELAXED) >= 64 * t->n))) {
        struct __cccc_task tk;
        memset(&tk, 0, sizeof(tk));
        tk.refs         = 1 << 30;
        tk.final        = (flags & 2) || (cur && cur->final);
        tk.data         = data;
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
    tk->refs       = 1;

    pthread_mutex_lock(&t->tm);
    cur->refs++;
    __atomic_add_fetch(&cur->children, 1, __ATOMIC_RELAXED);
    cur->kids = 1;
    tk->group = cur->group;
    if (tk->group)
        tk->group->count++;
    t->live++;
    if (nd)
        __cccc_task_deps(tk, deps, nd);
    if (tk->undeferred) {
        while (tk->npred > 0) {
            struct __cccc_task *other = __cccc_task_pop(t, cur, 0);
            if (other) {
                pthread_mutex_unlock(&t->tm);
                __cccc_task_run(t, other);
                pthread_mutex_lock(&t->tm);
            } else
                __cccc_task_wait(t);
        }
        pthread_mutex_unlock(&t->tm);
        __cccc_task_run(t, tk);
        return;
    }
    if (tk->npred == 0)
        __cccc_task_push(t, tk);
    pthread_mutex_unlock(&t->tm);
}

// Runs the calling thread's own children, newest first, until they are done.
void __cccc_omp_taskwait(void) {
    struct __cccc_team *t   = __cccc_tls_team;
    struct __cccc_task *cur = __cccc_tls_task;
    if (!t || t->n == 1 || !cur ||
        !__atomic_load_n(&cur->children, __ATOMIC_RELAXED))
        return;
    pthread_mutex_lock(&t->tm);
    while (cur->children > 0) {
        struct __cccc_task *tk = __cccc_task_pop(t, cur, 0);
        if (tk) {
            pthread_mutex_unlock(&t->tm);
            __cccc_task_run(t, tk);
            pthread_mutex_lock(&t->tm);
        } else
            __cccc_task_wait(t);
    }
    pthread_mutex_unlock(&t->tm);
}

void *__cccc_omp_taskgroup_begin(void) {
    struct __cccc_team *t = __cccc_tls_team;
    if (!t || t->n == 1)
        return 0;
    struct __cccc_task      *cur = __cccc_task_current();
    struct __cccc_taskgroup *g   = calloc(1, sizeof(*g));
    if (!g)
        __builtin_trap();
    pthread_mutex_lock(&t->tm);
    g->parent  = cur->group;
    cur->group = g;
    pthread_mutex_unlock(&t->tm);
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
    pthread_mutex_lock(&t->tm);
    while (g->count > 0) {
        struct __cccc_task *tk = __cccc_task_pop(t, cur, g);
        if (tk) {
            pthread_mutex_unlock(&t->tm);
            __cccc_task_run(t, tk);
            pthread_mutex_lock(&t->tm);
        } else
            __cccc_task_wait(t);
    }
    cur->group = g->parent;
    pthread_mutex_unlock(&t->tm);
    free(g);
}

// A thread that reaches the barrier runs queued tasks until every thread has
// arrived and no task is left.
void __cccc_omp_barrier(void) {
    struct __cccc_team *t = __cccc_tls_team;
    if (!t || t->n == 1)
        return;
    pthread_mutex_lock(&t->tm);
    unsigned gen = t->bgen;
    t->barrived++;
    for (;;) {
        if (t->barrived == t->n && t->live == 0) {
            t->barrived = 0;
            t->bgen++;
            __cccc_task_wake(t);
            break;
        }
        if (gen != t->bgen)
            break;
        struct __cccc_task *tk = __cccc_task_pop(t, 0, 0);
        if (tk) {
            pthread_mutex_unlock(&t->tm);
            __cccc_task_run(t, tk);
            pthread_mutex_lock(&t->tm);
        } else
            __cccc_task_wait(t);
    }
    pthread_mutex_unlock(&t->tm);
}

// The end of a parallel region: runs tasks until none is left, then drops the
// thread's implicit task.
void __cccc_omp_task_drain(void) {
    struct __cccc_team *t = __cccc_tls_team;
    if (t && t->n > 1) {
        pthread_mutex_lock(&t->tm);
        while (t->live > 0) {
            struct __cccc_task *tk = __cccc_task_pop(t, 0, 0);
            if (tk) {
                pthread_mutex_unlock(&t->tm);
                __cccc_task_run(t, tk);
                pthread_mutex_lock(&t->tm);
            } else
                __cccc_task_wait(t);
        }
        pthread_mutex_unlock(&t->tm);
    }
    if (__cccc_tls_task) {
        if (t)
            pthread_mutex_lock(&t->tm);
        __cccc_task_free_deps(__cccc_tls_task);
        if (t)
            pthread_mutex_unlock(&t->tm);
        free(__cccc_tls_task);
        __cccc_tls_task = 0;
    }
}
