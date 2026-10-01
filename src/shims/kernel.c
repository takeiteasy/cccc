// -c=native/-m kernel runtime (#1394): the cccc_* work-item builtins and the
// launcher behind cccc_launch, the C counterpart of src/stdlib/kernel.c. Runs
// on the pool and barrier in pool.c.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating lives in serialize_threaded_shims() in
// src/serialize_shims.c; the kernel table the launcher looks up is emitted
// after every function by serialize_kernel_meta().

// >>> shim: includes
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// <<< shim

// >>> shim: runtime
// Layout of cccc_range.
struct __cccc_krange {
    unsigned dims;
    size_t   size[3];
};

struct __cccc_klaunch {
    unsigned dims;
    size_t   global[3], local[3], groups[3];
    size_t   arg_off[8]; // CCCC_LOCAL argument i: offset into the group block
};

struct __cccc_kitem {
    const struct __cccc_klaunch *launch;
    size_t                       global_id[3], local_id[3], group_id[3];
    char                        *group_local;
    __cccc_barrier_t            *barrier;
};

// The work-item the calling thread runs, or 0 outside a launch.
static __thread struct __cccc_kitem *__cccc_kcur;

static int __cccc_kernel_meta_find(void *fn, int *uses_barrier,
                                   int *local_bytes);

static void __cccc_kernel_fatal(const char *fmt, ...) {
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "error: cccc_launch: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(255);
}

// Outside a launch a kernel is a single work-item: ids 0, sizes 1.
static size_t __cccc_kquery(const size_t *values, unsigned dim, unsigned dims,
                            size_t otherwise) {
    return dim < 3 && dim < dims ? values[dim] : otherwise;
}

size_t cccc_global_id(unsigned dim) {
    struct __cccc_kitem *it = __cccc_kcur;
    return it ? __cccc_kquery(it->global_id, dim, it->launch->dims, 0) : 0;
}

size_t cccc_local_id(unsigned dim) {
    struct __cccc_kitem *it = __cccc_kcur;
    return it ? __cccc_kquery(it->local_id, dim, it->launch->dims, 0) : 0;
}

size_t cccc_group_id(unsigned dim) {
    struct __cccc_kitem *it = __cccc_kcur;
    return it ? __cccc_kquery(it->group_id, dim, it->launch->dims, 0) : 0;
}

size_t cccc_global_size(unsigned dim) {
    struct __cccc_kitem *it = __cccc_kcur;
    return it ? __cccc_kquery(it->launch->global, dim, it->launch->dims, 1) : 1;
}

size_t cccc_local_size(unsigned dim) {
    struct __cccc_kitem *it = __cccc_kcur;
    return it ? __cccc_kquery(it->launch->local, dim, it->launch->dims, 1) : 1;
}

size_t cccc_num_groups(unsigned dim) {
    struct __cccc_kitem *it = __cccc_kcur;
    return it ? __cccc_kquery(it->launch->groups, dim, it->launch->dims, 1) : 1;
}

unsigned cccc_work_dim(void) {
    struct __cccc_kitem *it = __cccc_kcur;
    return it ? it->launch->dims : 1;
}

void cccc_barrier(unsigned fences) {
    (void)fences;
    struct __cccc_kitem *it = __cccc_kcur;
    if (it && it->barrier)
        __cccc_barrier_wait(it->barrier);
}

static size_t __cccc_kalign(size_t n) {
    return (n + 15) & ~(size_t)15;
}

static void __cccc_kread_range(const struct __cccc_krange *range,
                               const char *what, unsigned *dims,
                               size_t out[3]) {
    if (!range || range->dims < 1 || range->dims > 3)
        __cccc_kernel_fatal("%s range must have 1 to 3 dimensions", what);
    *dims = range->dims;
    for (unsigned d = 0; d < 3; d++) {
        out[d] = d < range->dims ? range->size[d] : 1;
        if (d < range->dims && range->size[d] == 0)
            __cccc_kernel_fatal("%s range has a zero size in dimension %u",
                                what, d);
    }
}

struct __cccc_kjob {
    void (*thunk)(void *);
    void                 *env;
    struct __cccc_klaunch launch;
    size_t                group_items, total_groups, local_total;
    size_t                next_group;
};

static void __cccc_kset_item(struct __cccc_kitem      *item,
                             const struct __cccc_kjob *job, size_t group,
                             size_t flat, char *group_local,
                             __cccc_barrier_t *barrier) {
    const struct __cccc_klaunch *l = &job->launch;
    item->launch                   = l;
    item->group_local              = group_local;
    item->barrier                  = barrier;
    for (int d = 0; d < 3; d++) {
        item->group_id[d]  = group % l->groups[d];
        group             /= l->groups[d];
        item->local_id[d]  = flat % l->local[d];
        flat              /= l->local[d];
        item->global_id[d] =
            item->group_id[d] * l->local[d] + item->local_id[d];
    }
}

// A thread of the pool takes whole groups, each run as a loop over its
// work-items. A kernel with no barrier needs nothing more.
static void __cccc_kworker(void *p) {
    struct __cccc_kjob  *job   = p;
    struct __cccc_kitem *saved = __cccc_kcur;
    struct __cccc_kitem  item;
    char *local = job->local_total ? malloc(job->local_total) : 0;
    if (job->local_total && !local)
        __cccc_kernel_fatal("out of memory for work-group local memory");
    for (;;) {
        size_t group =
            __atomic_fetch_add(&job->next_group, 1, __ATOMIC_RELAXED);
        if (group >= job->total_groups)
            break;
        if (local)
            memset(local, 0, job->local_total);
        for (size_t flat = 0; flat < job->group_items; flat++) {
            __cccc_kset_item(&item, job, group, flat, local, 0);
            __cccc_kcur = &item;
            job->thunk(job->env);
        }
    }
    __cccc_kcur = saved;
    free(local);
}

struct __cccc_kthread {
    const struct __cccc_kjob *job;
    struct __cccc_kitem       item;
    pthread_t                 thread;
};

static void *__cccc_kitem_thread(void *p) {
    struct __cccc_kthread *t = p;
    __cccc_kcur              = &t->item;
    t->job->thunk(t->job->env);
    __cccc_barrier_leave(t->item.barrier);
    return 0;
}

// TODO: a thread per work-item, created and joined for every group; split the
// kernel at its barriers into work-item loops instead (#1385).
static void __cccc_krun_group_threads(struct __cccc_kjob *job, size_t group) {
    size_t                 n       = job->group_items;
    struct __cccc_kthread *threads = calloc(n, sizeof(*threads));
    char            *local = job->local_total ? calloc(1, job->local_total) : 0;
    __cccc_barrier_t barrier;
    if (!threads || (job->local_total && !local))
        __cccc_kernel_fatal("out of memory");
    __cccc_barrier_init(&barrier, (int)n);
    for (size_t flat = 0; flat < n; flat++) {
        threads[flat].job = job;
        __cccc_kset_item(&threads[flat].item, job, group, flat, local,
                         &barrier);
        if (pthread_create(&threads[flat].thread, 0, __cccc_kitem_thread,
                           &threads[flat]))
            __cccc_kernel_fatal("cannot create a work-item thread");
    }
    for (size_t flat = 0; flat < n; flat++)
        pthread_join(threads[flat].thread, 0);
    __cccc_barrier_destroy(&barrier);
    free(local);
    free(threads);
}

// Runs `thunk(env)`, which calls the kernel, once per work-item. `sizes` holds
// the byte size of each CCCC_LOCAL argument.
void __cccc_kernel_launch(void (*thunk)(void *), void *env, void *kernel,
                          void *globalp, void *localp, long nlocal,
                          void *sizesp) {
    const long *sizes = sizesp;
    int         uses_barrier, local_bytes;
    if (!__cccc_kernel_meta_find(kernel, &uses_barrier, &local_bytes))
        __cccc_kernel_fatal("launching a function that is not a "
                            "[[cccc::kernel]]");

    struct __cccc_kjob job = {thunk, env};
    unsigned           local_dims;
    __cccc_kread_range(globalp, "global", &job.launch.dims, job.launch.global);
    __cccc_kread_range(localp, "local", &local_dims, job.launch.local);
    if (local_dims != job.launch.dims)
        __cccc_kernel_fatal(
            "global range has %u dimension(s), local range has %u",
            job.launch.dims, local_dims);

    job.group_items  = 1;
    job.total_groups = 1;
    for (int d = 0; d < 3; d++) {
        if (job.launch.global[d] % job.launch.local[d])
            __cccc_kernel_fatal("local size %zu does not divide global size "
                                "%zu in dimension %d",
                                job.launch.local[d], job.launch.global[d], d);
        job.launch.groups[d]  = job.launch.global[d] / job.launch.local[d];
        job.group_items      *= job.launch.local[d];
        job.total_groups     *= job.launch.groups[d];
    }
    if (job.group_items > (size_t)__CCCC_KERNEL_MAX_GROUP)
        __cccc_kernel_fatal("work-group of %zu items exceeds the limit of %zu "
                            "(--kernel-max-group-size)",
                            job.group_items, (size_t)__CCCC_KERNEL_MAX_GROUP);

    job.local_total = __cccc_kalign((size_t)local_bytes);
    for (long i = 0; i < nlocal; i++) {
        job.launch.arg_off[i]  = job.local_total;
        job.local_total       += __cccc_kalign((size_t)sizes[i]);
    }

    if (uses_barrier) {
        for (size_t group = 0; group < job.total_groups; group++)
            __cccc_krun_group_threads(&job, group);
        return;
    }
    int threads = __cccc_pool_default_threads();
    if ((size_t)threads > job.total_groups)
        threads = (int)job.total_groups;
    __cccc_pool_run(__cccc_kworker, &job, threads);
}

void *__cccc_kernel_local_arg(long i) {
    struct __cccc_kitem *it = __cccc_kcur;
    return it->group_local + it->launch->arg_off[i];
}

static pthread_mutex_t __cccc_kdirect_m = PTHREAD_MUTEX_INITIALIZER;
static void          **__cccc_kdirect;
static long            __cccc_kdirect_n;

// Address of a [[cccc::local]] object. Outside a launch (a kernel called like
// an ordinary function) each object gets its own block.
void *__cccc_local_base(long off, long size, long id) {
    struct __cccc_kitem *it = __cccc_kcur;
    if (it && it->group_local)
        return it->group_local + off;
    pthread_mutex_lock(&__cccc_kdirect_m);
    if (id >= __cccc_kdirect_n) {
        void **grown =
            realloc(__cccc_kdirect, (size_t)(id + 1) * sizeof(void *));
        if (!grown)
            __cccc_kernel_fatal("out of memory for local memory");
        memset(grown + __cccc_kdirect_n, 0,
               (size_t)(id + 1 - __cccc_kdirect_n) * sizeof(void *));
        __cccc_kdirect   = grown;
        __cccc_kdirect_n = id + 1;
    }
    if (!__cccc_kdirect[id] && !(__cccc_kdirect[id] = calloc(1, (size_t)size)))
        __cccc_kernel_fatal("out of memory for local memory");
    void *block = __cccc_kdirect[id];
    pthread_mutex_unlock(&__cccc_kdirect_m);
    return block;
}
// <<< shim
