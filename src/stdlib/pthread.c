// GIL-backed POSIX pthread wrapper for CCCC VM code.
#include "../cccc.h"
#include "../internal.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <errno.h>
#include <pthread.h>
#include <string.h>

// Mirrors TSS_DTOR_ITERATIONS in include/threads.h (the guest-visible C11
// macro) -- this is the host-side implementation file, compiled independently
// of the guest stdlib headers, so the value is duplicated rather than shared.
#define CCCC_TSS_DTOR_ITERATIONS 4

typedef struct CCCCUserMutex {
    void *handle;
    long  state;
    int   type; // 0 (default/normal) or PTHREAD_MUTEX_RECURSIVE/etc, set via
                // pthread_mutexattr_settype() before the first lock/init.
} CCCCUserMutex;

// Guest-visible pthread_mutexattr_t (include/pthread.h): { int __type; }
typedef struct CCCCUserMutexAttr {
    int type;
} CCCCUserMutexAttr;

typedef struct CCCCUserCond {
    void *handle;
    long  state;
} CCCCUserCond;

typedef struct CCCCUserAttr {
    size_t stack_size;
    void  *stack_addr;
} CCCCUserAttr;

typedef struct CCCCPthreadValue {
    int                      key;
    void                    *value;
    struct CCCCPthreadValue *next;
} CCCCPthreadValue;

struct PthreadKeyRecord {
    int key;
    // Guest byte offset (or FFI token) for the registered destructor -- NOT a
    // host-callable pointer. Must be invoked via cccc_call_guest_callback,
    // never called directly (see run_tss_destructors below).
    long long                destructor_value;
    int                      deleted;
    struct PthreadKeyRecord *next;
};

struct ThreadRecord {
    pthread_t            host_thread;
    VirtualMachine      *vm;
    ExecState            exec;
    long long            start_fn;
    void                *arg;
    void                *retval;
    int                  vm_rc;
    int                  exited;
    int                  detached;
    int                  joined;
    CCCCPthreadValue    *values;
    struct ThreadRecord *next;
    // Lock-order tracking (CCCC_THREAD_SAFETY)
    void **held_locks; // array of CCCCUserMutex* currently held
    int    held_locks_count;
    int    held_locks_cap;
    // Per-thread TLS segment (copy of vm->tls_template made at thread creation)
    char *tls_seg;
    // Set when THIS thread called pthread_exit()/thrd_exit() itself (as
    // opposed to just returning from its start function). Only meaningful
    // for the main thread's record today: cc_run (vm.c) checks it after
    // cc_run_at(main) returns to decide whether to drain TSS/pthread-key
    // destructors for main, since POSIX/glibc run them for an explicit
    // pthread_exit() but not for a plain `return` from main() (#863).
    int exited_via_pthread_exit;
};

struct PthreadState {
    ThreadRecord main_thread;
};

static VirtualMachine *current_vm(void) {
    return cccc_current_ffi_vm();
}

static void enable_pthread_runtime(VirtualMachine *vm) {
    if (!vm)
        return;
    // The canary frame-layout shift is baked into stack offsets at compile time
    // (see assign_stack_offsets, #445), so the CCCC_STACK_CANARIES flag must
    // not be toggled at runtime — doing so would desync ENT3 from the baked
    // offsets. Stack canaries are therefore supported during threading.
    (void)vm;
}

static PthreadState *pthread_state(VirtualMachine *vm) {
    if (!vm->pthread_state) {
        vm->pthread_state = calloc(1, sizeof(PthreadState));
        if (!vm->pthread_state)
            return NULL;
        vm->pthread_state->main_thread.vm          = vm;
        vm->pthread_state->main_thread.host_thread = pthread_self();
        vm->thread_records = &vm->pthread_state->main_thread;
    }
    return vm->pthread_state;
}

static ThreadRecord *current_thread(VirtualMachine *vm) {
    PthreadState *state = pthread_state(vm);
    if (!state)
        return NULL;
    return vm->active_thread ? vm->active_thread : &state->main_thread;
}

static void save_and_release_gil(VirtualMachine *vm, ExecState *state) {
    cccc_exec_state_save(vm, state);
    cccc_gil_release(vm);
}

static void acquire_and_restore_gil(VirtualMachine  *vm,
                                    const ExecState *state) {
    cccc_gil_acquire(vm);
    cccc_exec_state_restore(vm, state);
}

static void link_thread(VirtualMachine *vm, ThreadRecord *rec) {
    rec->next          = vm->thread_records;
    vm->thread_records = rec;
}

static void unlink_thread(VirtualMachine *vm, ThreadRecord *rec) {
    ThreadRecord **cur = &vm->thread_records;
    while (*cur) {
        if (*cur == rec) {
            *cur      = rec->next;
            rec->next = NULL;
            return;
        }
        cur = &(*cur)->next;
    }
}

static void free_thread_record(ThreadRecord *rec) {
    if (!rec)
        return;
    CCCCPthreadValue *value = rec->values;
    while (value) {
        CCCCPthreadValue *next = value->next;
        free(value);
        value = next;
    }
    free(rec->held_locks);
    free(rec->tls_seg);
    rec->tls_seg = NULL;
    cccc_exec_state_release_stack(rec->vm, &rec->exec);
    free(rec);
}

// ---------------------------------------------------------------------------
// Thread-safety helpers (CCCC_THREAD_SAFETY)
// ---------------------------------------------------------------------------

int cccc_thread_held_lock_count(VirtualMachine *vm) {
    if (!vm || !vm->pthread_state)
        return 0;
    ThreadRecord *tr =
        vm->active_thread ? vm->active_thread : &vm->pthread_state->main_thread;
    return tr->held_locks_count;
}

static void thread_add_held_lock(ThreadRecord *tr, void *mutex) {
    if (tr->held_locks_count >= tr->held_locks_cap) {
        int    new_cap = tr->held_locks_cap ? tr->held_locks_cap * 2 : 4;
        void **new_arr =
            realloc(tr->held_locks, (size_t)new_cap * sizeof(void *));
        if (!new_arr)
            return;
        tr->held_locks     = new_arr;
        tr->held_locks_cap = new_cap;
    }
    tr->held_locks[tr->held_locks_count++] = mutex;
}

static void thread_remove_held_lock(ThreadRecord *tr, void *mutex) {
    for (int i = 0; i < tr->held_locks_count; i++) {
        if (tr->held_locks[i] == mutex) {
            tr->held_locks[i] = tr->held_locks[--tr->held_locks_count];
            return;
        }
    }
}

// Returns 1 if the edge (from -> to) exists in the lock graph.
static int lock_graph_has_edge(VirtualMachine *vm, void *from, void *to) {
    for (int i = 0; i < vm->lock_graph_size; i++) {
        if (vm->lock_graph_from[i] == from && vm->lock_graph_to[i] == to)
            return 1;
    }
    return 0;
}

static void lock_graph_add_edge(VirtualMachine *vm, void *from, void *to) {
    if (lock_graph_has_edge(vm, from, to))
        return;
    if (vm->lock_graph_size >= vm->lock_graph_cap) {
        int    new_cap = vm->lock_graph_cap ? vm->lock_graph_cap * 2 : 8;
        void **new_from =
            realloc(vm->lock_graph_from, (size_t)new_cap * sizeof(void *));
        void **new_to =
            realloc(vm->lock_graph_to, (size_t)new_cap * sizeof(void *));
        if (!new_from || !new_to) {
            free(new_from);
            free(new_to);
            return;
        }
        vm->lock_graph_from = new_from;
        vm->lock_graph_to   = new_to;
        vm->lock_graph_cap  = new_cap;
    }
    vm->lock_graph_from[vm->lock_graph_size] = from;
    vm->lock_graph_to[vm->lock_graph_size]   = to;
    vm->lock_graph_size++;
}

// Returns 1 if the lock attempt should be aborted (double-lock), 0 otherwise.
static int check_lock_safety(VirtualMachine *vm, ThreadRecord *tr,
                             void *mutex) {
    // Double-lock check: print diagnostic and return 1 so the caller returns
    // EDEADLK immediately instead of blocking forever on a non-recursive mutex.
    for (int i = 0; i < tr->held_locks_count; i++) {
        if (tr->held_locks[i] == mutex) {
            fprintf(stderr,
                    "\n====== DEADLOCK: double-lock detected ======\n"
                    "Thread %p attempted to lock mutex %p a second time\n"
                    "without an intervening unlock (non-recursive mutex).\n"
                    "============================================\n",
                    (void *)tr, mutex);
            return 1;
        }
    }
    // Lock-order inversion check: for each lock H currently held, see if the
    // reverse edge (mutex -> H) already exists in the graph.
    for (int i = 0; i < tr->held_locks_count; i++) {
        void *held = tr->held_locks[i];
        if (lock_graph_has_edge(vm, mutex, held)) {
            fprintf(stderr,
                    "\n====== LOCK ORDER INVERSION detected ======\n"
                    "Thread %p is acquiring mutex %p while holding %p,\n"
                    "but another thread previously acquired them in the "
                    "opposite order.\nThis is a potential deadlock.\n"
                    "===========================================\n",
                    (void *)tr, mutex, held);
        }
        lock_graph_add_edge(vm, held, mutex);
    }
    return 0;
}

// Forward declaration -- defined below alongside the rest of the TSS/key
// machinery (find_key, wrap_pthread_key_create, etc). Invokes every
// tss_create/pthread_key_create destructor still owed to `rec` per C11
// 7.26.6.1p2, run from vm_thread_start below while the worker's VM context
// (active_thread, current_tls_seg, sp/bp) is still live.
static void run_tss_destructors(VirtualMachine *vm, ThreadRecord *rec);

static void *vm_thread_start(void *arg) {
    ThreadRecord   *rec = arg;
    VirtualMachine *vm  = rec->vm;

    cccc_gil_acquire(vm);
    ThreadRecord *saved_active  = vm->active_thread;
    char         *saved_tls_seg = vm->current_tls_seg;
    vm->active_thread           = rec;
    // Point current_tls_seg at this thread's private TLS copy
    if (rec->tls_seg)
        vm->current_tls_seg = rec->tls_seg;
    cccc_exec_state_restore(vm, &rec->exec);
    // Stack canaries stay enabled in threads: the frame-layout shift is baked
    // into stack offsets at compile time, so the flag must match what codegen
    // assumed (#445).
    rec->vm_rc  = vm_eval(vm);
    rec->retval = (void *)vm->regs[REG_A0];
    rec->exited = 1;
    // Run TSS destructors here, before anything below unwinds this thread's
    // VM context -- this is the only point where active_thread == rec,
    // current_tls_seg == rec->tls_seg, and sp/bp are still the worker's, all
    // of which cccc_call_guest_callback requires (see its contract in
    // internal.h). A plain `return` from main() intentionally does NOT reach
    // here (matches glibc); pthread_exit() called ON the main thread also
    // doesn't run key destructors in cccc today -- that's a separate,
    // post-GIL-release teardown path, tracked as a followup ticket.
    run_tss_destructors(vm, rec);
    cccc_exec_state_save(vm, &rec->exec);
    vm->active_thread   = saved_active;
    vm->current_tls_seg = saved_tls_seg;

    if (rec->detached) {
        unlink_thread(vm, rec);
        cccc_gil_release(vm);
        free_thread_record(rec);
        return NULL;
    }

    cccc_gil_release(vm);
    return rec->retval;
}

static long long wrap_pthread_create(long long threadp, long long attrp,
                                     long long start_fn, long long arg) {
    VirtualMachine *vm = current_vm();
    if (!vm || !threadp || !start_fn)
        return EINVAL;

    Pc entry = cc_byte_offset_to_pc(start_fn);
    if (entry == CCCC_INVALID_PC || entry > vm->text_ptr)
        return EINVAL;

    enable_pthread_runtime(vm);

    ThreadRecord *rec = calloc(1, sizeof(*rec));
    if (!rec)
        return EAGAIN;
    rec->vm       = vm;
    rec->start_fn = start_fn;
    rec->arg      = (void *)arg;
    if (cccc_exec_state_alloc_stack(vm, &rec->exec) != 0) {
        free(rec);
        return EAGAIN;
    }
    // Allocate per-thread TLS copy initialised from the template. #1136:
    // posix_memalign, not malloc -- see vm.c's cc_run_at main-thread copy
    // for the same rationale (LDTLS3 needs the base itself aligned, not
    // just the offset into it).
    if (vm->tls_template_size > 0) {
        rec->tls_seg = NULL;
        if (posix_memalign((void **)&rec->tls_seg, CCCC_MAX_DATA_ALIGN,
                           vm->tls_template_size) != 0) {
            cccc_exec_state_release_stack(vm, &rec->exec);
            free(rec);
            return EAGAIN;
        }
        memcpy(rec->tls_seg, vm->tls_template, vm->tls_template_size);
    }
    cccc_exec_state_prepare_call(vm, &rec->exec, entry, arg);
    link_thread(vm, rec);

    pthread_attr_t  host_attr;
    pthread_attr_t *host_attr_ptr = NULL;
    CCCCUserAttr   *user_attr     = (CCCCUserAttr *)attrp;
    if (user_attr && user_attr->stack_size) {
        if (pthread_attr_init(&host_attr) == 0) {
            pthread_attr_setstacksize(&host_attr, user_attr->stack_size);
            host_attr_ptr = &host_attr;
        }
    }

    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    int rc =
        pthread_create(&rec->host_thread, host_attr_ptr, vm_thread_start, rec);
    acquire_and_restore_gil(vm, &caller_state);
    if (host_attr_ptr)
        pthread_attr_destroy(host_attr_ptr);

    if (rc != 0) {
        unlink_thread(vm, rec);
        free_thread_record(rec);
        return rc;
    }

    *(void **)threadp = rec;
    hashmap_put_int(&vm->init_state, threadp, (void *)1);
    return 0;
}

static long long wrap_pthread_join(long long thread, long long retvalp) {
    VirtualMachine *vm  = current_vm();
    ThreadRecord   *rec = (ThreadRecord *)thread;
    if (!vm || !rec || rec->joined || rec->detached)
        return EINVAL;

    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    void *retval = NULL;
    int   rc     = pthread_join(rec->host_thread, &retval);
    acquire_and_restore_gil(vm, &caller_state);
    if (rc != 0)
        return rc;

    rec->joined = 1;
    if (retvalp) {
        *(void **)retvalp = rec->retval ? rec->retval : retval;
        hashmap_put_int(&vm->init_state, retvalp, (void *)1);
    }
    unlink_thread(vm, rec);
    free_thread_record(rec);
    return 0;
}

static long long wrap_pthread_detach(long long thread) {
    ThreadRecord *rec = (ThreadRecord *)thread;
    if (!rec || rec->joined)
        return EINVAL;
    int rc = pthread_detach(rec->host_thread);
    if (rc == 0) {
        rec->detached = 1;
        if (rec->exited) {
            VirtualMachine *vm = current_vm();
            if (vm)
                unlink_thread(vm, rec);
            free_thread_record(rec);
        }
    }
    return rc;
}

static long long wrap_pthread_exit(long long retval) {
    VirtualMachine *vm = current_vm();
    if (!vm)
        return 0;
    ThreadRecord *rec = current_thread(vm);
    if (rec) {
        rec->retval                  = (void *)retval;
        rec->exited_via_pthread_exit = 1;
    }
    vm->regs[REG_A0] = retval;
    vm->pc           = CCCC_INVALID_PC;
    return 0;
}

static long long wrap_pthread_self(void) {
    VirtualMachine *vm  = current_vm();
    ThreadRecord   *rec = vm ? current_thread(vm) : NULL;
    return (long long)rec;
}

static long long wrap_pthread_equal(long long a, long long b) {
    return a == b;
}

static pthread_mutex_t *ensure_mutex(CCCCUserMutex *mutex) {
    if (!mutex)
        return NULL;
    if (!mutex->handle) {
        pthread_mutex_t *host = malloc(sizeof(*host));
        if (!host)
            return NULL;
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        if (mutex->type != 0)
            pthread_mutexattr_settype(&attr, mutex->type);
        int rc = pthread_mutex_init(host, &attr);
        pthread_mutexattr_destroy(&attr);
        if (rc != 0) {
            free(host);
            return NULL;
        }
        mutex->handle = host;
        mutex->state  = 1;
    }
    return (pthread_mutex_t *)mutex->handle;
}

static pthread_cond_t *ensure_cond(CCCCUserCond *cond) {
    if (!cond)
        return NULL;
    if (!cond->handle) {
        pthread_cond_t *host = malloc(sizeof(*host));
        if (!host)
            return NULL;
        if (pthread_cond_init(host, NULL) != 0) {
            free(host);
            return NULL;
        }
        cond->handle = host;
        cond->state  = 1;
    }
    return (pthread_cond_t *)cond->handle;
}

static long long wrap_pthread_mutex_init(long long mutexp, long long attrp) {
    CCCCUserMutex *mutex = (CCCCUserMutex *)mutexp;
    if (!mutex)
        return EINVAL;
    if (mutex->handle)
        return EBUSY;
    CCCCUserMutexAttr *attr = (CCCCUserMutexAttr *)attrp;
    mutex->type             = attr ? attr->type : 0;
    pthread_mutex_t *host   = ensure_mutex(mutex);
    return host ? 0 : EAGAIN;
}

static long long wrap_pthread_mutex_destroy(long long mutexp) {
    CCCCUserMutex *mutex = (CCCCUserMutex *)mutexp;
    if (!mutex || !mutex->handle)
        return EINVAL;
    int rc = pthread_mutex_destroy((pthread_mutex_t *)mutex->handle);
    if (rc == 0) {
        free(mutex->handle);
        mutex->handle = NULL;
        mutex->state  = 0;
    }
    return rc;
}

static long long wrap_pthread_mutex_lock(long long mutexp) {
    VirtualMachine  *vm    = current_vm();
    CCCCUserMutex   *mutex = (CCCCUserMutex *)mutexp;
    pthread_mutex_t *host  = ensure_mutex(mutex);
    if (!vm || !host)
        return EINVAL;
    // Recursive mutexes are legitimately re-locked by the same thread; skip
    // the double-lock deadlock diagnostic for them (#623).
    if ((vm->flags & CCCC_THREAD_SAFETY) &&
        mutex->type != PTHREAD_MUTEX_RECURSIVE) {
        ThreadRecord *tr = current_thread(vm);
        if (tr && check_lock_safety(vm, tr, (void *)mutexp))
            return EDEADLK;
    }
    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    int rc = pthread_mutex_lock(host);
    acquire_and_restore_gil(vm, &caller_state);
    if (rc == 0 && (vm->flags & CCCC_THREAD_SAFETY)) {
        ThreadRecord *tr = current_thread(vm);
        if (tr)
            thread_add_held_lock(tr, (void *)mutexp);
    }
    return rc;
}

static long long wrap_pthread_mutex_trylock(long long mutexp) {
    VirtualMachine  *vm   = current_vm();
    pthread_mutex_t *host = ensure_mutex((CCCCUserMutex *)mutexp);
    if (!host)
        return EINVAL;
    int rc = pthread_mutex_trylock(host);
    if (rc == 0 && vm && (vm->flags & CCCC_THREAD_SAFETY)) {
        ThreadRecord *tr = current_thread(vm);
        if (tr)
            thread_add_held_lock(tr, (void *)mutexp);
    }
    return rc;
}

static long long wrap_pthread_mutex_unlock(long long mutexp) {
    VirtualMachine *vm    = current_vm();
    CCCCUserMutex  *mutex = (CCCCUserMutex *)mutexp;
    if (!mutex || !mutex->handle)
        return EINVAL;
    int rc = pthread_mutex_unlock((pthread_mutex_t *)mutex->handle);
    if (rc == 0 && vm && (vm->flags & CCCC_THREAD_SAFETY)) {
        ThreadRecord *tr = current_thread(vm);
        if (tr)
            thread_remove_held_lock(tr, (void *)mutexp);
    }
    return rc;
}

static long long wrap_pthread_mutexattr_init(long long attrp) {
    CCCCUserMutexAttr *attr = (CCCCUserMutexAttr *)attrp;
    if (!attr)
        return EINVAL;
    attr->type = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

static long long wrap_pthread_mutexattr_destroy(long long attrp) {
    (void)attrp;
    return 0;
}

static long long wrap_pthread_mutexattr_settype(long long attrp,
                                                long long type) {
    CCCCUserMutexAttr *attr = (CCCCUserMutexAttr *)attrp;
    if (!attr)
        return EINVAL;
    attr->type = (int)type;
    return 0;
}

static long long wrap_pthread_mutexattr_gettype(long long attrp,
                                                long long typep) {
    CCCCUserMutexAttr *attr = (CCCCUserMutexAttr *)attrp;
    int               *out  = (int *)typep;
    if (!attr || !out)
        return EINVAL;
    *out = attr->type;
    return 0;
}

static long long wrap_pthread_cond_init(long long condp, long long attrp) {
    (void)attrp;
    CCCCUserCond *cond = (CCCCUserCond *)condp;
    if (!cond)
        return EINVAL;
    if (cond->handle)
        return EBUSY;
    pthread_cond_t *host = ensure_cond(cond);
    return host ? 0 : EAGAIN;
}

static long long wrap_pthread_cond_destroy(long long condp) {
    CCCCUserCond *cond = (CCCCUserCond *)condp;
    if (!cond || !cond->handle)
        return EINVAL;
    int rc = pthread_cond_destroy((pthread_cond_t *)cond->handle);
    if (rc == 0) {
        free(cond->handle);
        cond->handle = NULL;
        cond->state  = 0;
    }
    return rc;
}

static long long wrap_pthread_cond_wait(long long condp, long long mutexp) {
    VirtualMachine  *vm    = current_vm();
    pthread_cond_t  *cond  = ensure_cond((CCCCUserCond *)condp);
    pthread_mutex_t *mutex = ensure_mutex((CCCCUserMutex *)mutexp);
    if (!vm || !cond || !mutex)
        return EINVAL;
    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    int rc = pthread_cond_wait(cond, mutex);
    acquire_and_restore_gil(vm, &caller_state);
    return rc;
}

static long long wrap_pthread_cond_timedwait(long long condp, long long mutexp,
                                             long long abstimep) {
    VirtualMachine  *vm    = current_vm();
    pthread_cond_t  *cond  = ensure_cond((CCCCUserCond *)condp);
    pthread_mutex_t *mutex = ensure_mutex((CCCCUserMutex *)mutexp);
    if (!vm || !cond || !mutex || !abstimep)
        return EINVAL;
    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    int rc =
        pthread_cond_timedwait(cond, mutex, (const struct timespec *)abstimep);
    acquire_and_restore_gil(vm, &caller_state);
    return rc;
}

static long long wrap_pthread_cond_signal(long long condp) {
    pthread_cond_t *cond = ensure_cond((CCCCUserCond *)condp);
    return cond ? pthread_cond_signal(cond) : EINVAL;
}

static long long wrap_pthread_cond_broadcast(long long condp) {
    pthread_cond_t *cond = ensure_cond((CCCCUserCond *)condp);
    return cond ? pthread_cond_broadcast(cond) : EINVAL;
}

static PthreadKeyRecord *find_key(VirtualMachine *vm, int key) {
    for (PthreadKeyRecord *rec = vm->pthread_keys; rec; rec = rec->next)
        if (rec->key == key)
            return rec;
    return NULL;
}

static long long wrap_pthread_key_create(long long keyp, long long destructor) {
    VirtualMachine *vm = current_vm();
    if (!vm || !keyp)
        return EINVAL;
    PthreadKeyRecord *rec = calloc(1, sizeof(*rec));
    if (!rec)
        return EAGAIN;
    rec->key              = ++vm->pthread_next_key;
    rec->destructor_value = destructor;
    rec->next             = vm->pthread_keys;
    vm->pthread_keys      = rec;
    *(unsigned int *)keyp = (unsigned int)rec->key;
    hashmap_put_int(&vm->init_state, keyp, (void *)1);
    return 0;
}

static long long wrap_pthread_key_delete(long long key) {
    VirtualMachine   *vm  = current_vm();
    PthreadKeyRecord *rec = vm ? find_key(vm, (int)key) : NULL;
    if (!rec || rec->deleted)
        return EINVAL;
    rec->deleted = 1;
    return 0;
}

static long long wrap_pthread_getspecific(long long key) {
    VirtualMachine *vm     = current_vm();
    ThreadRecord   *thread = vm ? current_thread(vm) : NULL;
    if (!vm || !thread)
        return 0;
    PthreadKeyRecord *keyrec = find_key(vm, (int)key);
    if (!keyrec || keyrec->deleted)
        return 0;
    for (CCCCPthreadValue *value = thread->values; value; value = value->next)
        if (value->key == (int)key)
            return (long long)value->value;
    return 0;
}

static long long wrap_pthread_setspecific(long long key, long long value) {
    VirtualMachine   *vm     = current_vm();
    ThreadRecord     *thread = vm ? current_thread(vm) : NULL;
    PthreadKeyRecord *keyrec = vm ? find_key(vm, (int)key) : NULL;
    if (!thread || !keyrec || keyrec->deleted)
        return EINVAL;
    for (CCCCPthreadValue *cur = thread->values; cur; cur = cur->next) {
        if (cur->key == (int)key) {
            cur->value = (void *)value;
            return 0;
        }
    }
    CCCCPthreadValue *cur = calloc(1, sizeof(*cur));
    if (!cur)
        return ENOMEM;
    cur->key       = (int)key;
    cur->value     = (void *)value;
    cur->next      = thread->values;
    thread->values = cur;
    return 0;
}

// C11 7.26.6.1p2 / 7.26.1p7: on thread exit, for each key whose associated
// value in this thread is non-NULL, call its destructor with that value.
// Re-checked up to TSS_DTOR_ITERATIONS passes since a destructor is allowed
// to tss_set() the same (or another) key again -- the slot is nulled out
// *before* the guest callback runs so a re-set during the callback is picked
// up by the next pass rather than lost or looped on forever. Deleted keys
// (tss_delete/pthread_key_delete) are skipped, matching find_key's existing
// deleted-gate in wrap_pthread_getspecific/setspecific.
//
// Must run mid-vm_eval with the GIL held and rec == vm->active_thread (see
// cccc_call_guest_callback's contract, internal.h) -- the only caller is
// vm_thread_start, before it restores the caller's VM context.
static void run_tss_destructors(VirtualMachine *vm, ThreadRecord *rec) {
    for (int pass = 0; pass < CCCC_TSS_DTOR_ITERATIONS; pass++) {
        int ran_any = 0;
        for (CCCCPthreadValue *value = rec->values; value;
             value                   = value->next) {
            if (!value->value)
                continue;
            PthreadKeyRecord *keyrec = find_key(vm, value->key);
            if (!keyrec || keyrec->deleted || !keyrec->destructor_value)
                continue;
            void *slot_value  = value->value;
            value->value      = NULL;
            long long args[1] = {(long long)slot_value};
            long long ignored;
            cccc_call_guest_callback(vm, keyrec->destructor_value, args, 1,
                                     &ignored);
            /* A faulting destructor doesn't abort the remaining keys, same
               rationale as drain_exit_handlers_nested (stdlib.c). */
            ran_any = 1;
        }
        if (!ran_any)
            break;
    }
}

// Invokes a single TSS/pthread-key destructor from a TOP-LEVEL (non-nested)
// context -- no GIL-held vm_eval on the C stack, so cccc_call_guest_callback
// cannot be used here (see its contract in internal.h). Mirrors the two
// dispatch cases cc_run_atexit_entries (vm.c) already handles for atexit
// handlers in this same kind of context: an FFI token (the destructor is
// itself an already-registered host function taken as a value, e.g.
// tss_create(&key, free) -- an idiomatic and fully valid C11 program) calls
// straight through via cccc_call_native_function; a guest byte offset runs
// via cc_run_at1, a complete top-level VM execution cycle.
static void call_tss_destructor_top_level(VirtualMachine *vm,
                                          long long       destructor_value,
                                          void           *arg) {
    if (destructor_value <= CCCC_FFI_TOKEN_BASE) {
        int ffi_idx = (int)(CCCC_FFI_TOKEN_BASE - destructor_value);
        if (ffi_idx < 0 || ffi_idx >= vm->compiler.ffi_count)
            return;
        ForeignFunc *ff        = &vm->compiler.ffi_table[ffi_idx];
        long long    argbuf[1] = {(long long)arg};
        cccc_call_native_function(vm, ff->func_ptr, ff->name, argbuf, 1, 0, 0,
                                  0, 0, ff->is_variadic, ff->num_fixed_args);
        return;
    }
    Pc entry = cc_byte_offset_to_pc(destructor_value);
    if (entry == CCCC_INVALID_PC || entry > vm->text_ptr)
        return;
    cc_run_at1(vm, entry, arg);
}

// Drains the main thread's TSS/pthread-key destructors after pthread_exit()
// was called ON THE MAIN THREAD -- POSIX/glibc run them in that case, unlike
// a plain `return` from main() (which must NOT run them; see #863 and the
// <threads.h> row in man/COVERAGE.md). Called exactly once, from cc_run
// (vm.c) right after cc_run_at(main) returns and before atexit handlers/
// destructors run -- a no-op if pthread_exit() was never called on main.
//
// Same re-check-up-to-CCCC_TSS_DTOR_ITERATIONS / null-before-call structure
// as run_tss_destructors above, just dispatched via
// call_tss_destructor_top_level instead of cccc_call_guest_callback since
// this runs post-GIL-release with no live vm_eval on the C stack.
void cccc_pthread_run_main_tss_destructors(VirtualMachine *vm) {
    if (!vm || !vm->pthread_state)
        return;
    ThreadRecord *main_thread = &vm->pthread_state->main_thread;
    if (!main_thread->exited_via_pthread_exit)
        return;
    main_thread->exited_via_pthread_exit = 0;
    for (int pass = 0; pass < CCCC_TSS_DTOR_ITERATIONS; pass++) {
        int ran_any = 0;
        for (CCCCPthreadValue *value = main_thread->values; value;
             value                   = value->next) {
            if (!value->value)
                continue;
            PthreadKeyRecord *keyrec = find_key(vm, value->key);
            if (!keyrec || keyrec->deleted || !keyrec->destructor_value)
                continue;
            void *slot_value = value->value;
            value->value     = NULL;
            call_tss_destructor_top_level(vm, keyrec->destructor_value,
                                          slot_value);
            ran_any = 1;
        }
        if (!ran_any)
            break;
    }
}

static long long wrap_pthread_attr_init(long long attrp) {
    if (!attrp)
        return EINVAL;
    memset((void *)attrp, 0, sizeof(CCCCUserAttr));
    return 0;
}

static long long wrap_pthread_attr_destroy(long long attrp) {
    return attrp ? 0 : EINVAL;
}

static long long wrap_pthread_attr_setstacksize(long long attrp,
                                                long long stacksize) {
    if (!attrp)
        return EINVAL;
    ((CCCCUserAttr *)attrp)->stack_size = (size_t)stacksize;
    return 0;
}

static long long wrap_pthread_attr_getstack(long long attrp,
                                            long long stackaddrp,
                                            long long stacksizep) {
    VirtualMachine *vm   = current_vm();
    CCCCUserAttr   *attr = (CCCCUserAttr *)attrp;
    if (!vm || !attr)
        return EINVAL;
    if (stackaddrp) {
        *(void **)stackaddrp = attr->stack_addr;
        hashmap_put_int(&vm->init_state, stackaddrp, (void *)1);
    }
    if (stacksizep) {
        *(size_t *)stacksizep = attr->stack_size;
        hashmap_put_int(&vm->init_state, stacksizep, (void *)1);
    }
    return 0;
}

void register_pthread_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "pthread_create", (void *)wrap_pthread_create, 4, 0);
    cc_register_cfunc(vm, "pthread_join", (void *)wrap_pthread_join, 2, 0);
    cc_register_cfunc(vm, "pthread_detach", (void *)wrap_pthread_detach, 1, 0);
    cc_register_cfunc(vm, "pthread_exit", (void *)wrap_pthread_exit, 1, 0);
    cc_register_cfunc(vm, "pthread_self", (void *)wrap_pthread_self, 0, 0);
    cc_register_cfunc(vm, "pthread_equal", (void *)wrap_pthread_equal, 2, 0);
    cc_register_cfunc(vm, "pthread_mutex_init", (void *)wrap_pthread_mutex_init,
                      2, 0);
    cc_register_cfunc(vm, "pthread_mutex_destroy",
                      (void *)wrap_pthread_mutex_destroy, 1, 0);
    cc_register_cfunc(vm, "pthread_mutex_lock", (void *)wrap_pthread_mutex_lock,
                      1, 0);
    cc_register_cfunc(vm, "pthread_mutex_trylock",
                      (void *)wrap_pthread_mutex_trylock, 1, 0);
    cc_register_cfunc(vm, "pthread_mutex_unlock",
                      (void *)wrap_pthread_mutex_unlock, 1, 0);
    cc_register_cfunc(vm, "pthread_mutexattr_init",
                      (void *)wrap_pthread_mutexattr_init, 1, 0);
    cc_register_cfunc(vm, "pthread_mutexattr_destroy",
                      (void *)wrap_pthread_mutexattr_destroy, 1, 0);
    cc_register_cfunc(vm, "pthread_mutexattr_settype",
                      (void *)wrap_pthread_mutexattr_settype, 2, 0);
    cc_register_cfunc(vm, "pthread_mutexattr_gettype",
                      (void *)wrap_pthread_mutexattr_gettype, 2, 0);
    cc_register_cfunc(vm, "pthread_cond_init", (void *)wrap_pthread_cond_init,
                      2, 0);
    cc_register_cfunc(vm, "pthread_cond_destroy",
                      (void *)wrap_pthread_cond_destroy, 1, 0);
    cc_register_cfunc(vm, "pthread_cond_wait", (void *)wrap_pthread_cond_wait,
                      2, 0);
    cc_register_cfunc(vm, "pthread_cond_timedwait",
                      (void *)wrap_pthread_cond_timedwait, 3, 0);
    cc_register_cfunc(vm, "pthread_cond_signal",
                      (void *)wrap_pthread_cond_signal, 1, 0);
    cc_register_cfunc(vm, "pthread_cond_broadcast",
                      (void *)wrap_pthread_cond_broadcast, 1, 0);
    cc_register_cfunc(vm, "pthread_key_create", (void *)wrap_pthread_key_create,
                      2, 0);
    cc_register_cfunc(vm, "pthread_key_delete", (void *)wrap_pthread_key_delete,
                      1, 0);
    cc_register_cfunc(vm, "pthread_getspecific",
                      (void *)wrap_pthread_getspecific, 1, 0);
    cc_register_cfunc(vm, "pthread_setspecific",
                      (void *)wrap_pthread_setspecific, 2, 0);
    cc_register_cfunc(vm, "pthread_attr_init", (void *)wrap_pthread_attr_init,
                      1, 0);
    cc_register_cfunc(vm, "pthread_attr_destroy",
                      (void *)wrap_pthread_attr_destroy, 1, 0);
    cc_register_cfunc(vm, "pthread_attr_setstacksize",
                      (void *)wrap_pthread_attr_setstacksize, 2, 0);
    cc_register_cfunc(vm, "pthread_attr_getstack",
                      (void *)wrap_pthread_attr_getstack, 3, 0);
}

// ---- C11 threads.h wrappers ----
// mtx_t layout is { void *__handle; long __state; int __type; } — the first
// two fields alias CCCCUserMutex so we can cast and reuse ensure_mutex.
// cnd_t layout { void *__handle; long __state; } aliases CCCCUserCond.

#include <sched.h>

static long long wrap_thrd_create(long long thrp, long long func,
                                  long long arg) {
    // thrd_create(thrd_t*, thrd_start_t, void*) — maps to pthread_create with
    // no attrs
    long long rc = wrap_pthread_create(thrp, 0, func, arg);
    if (rc == 0)
        return 0;      // thrd_success
    if (rc == ENOMEM)
        return ENOMEM; // thrd_nomem
    return 1;          // thrd_error
}

static long long wrap_thrd_join(long long thr, long long resp) {
    // thrd_join(thrd_t, int*res) — retval (int) stored in *res
    void     *retval_ptr = NULL;
    long long rv_slot    = (long long)&retval_ptr;
    long long rc         = wrap_pthread_join(thr, rv_slot);
    if (rc != 0)
        return 1; // thrd_error
    if (resp)
        *(int *)resp = (int)(long long)retval_ptr;
    return 0;     // thrd_success
}

static long long wrap_thrd_exit(long long code) {
    return wrap_pthread_exit(code);
}

static long long wrap_thrd_detach(long long thr) {
    long long rc = wrap_pthread_detach(thr);
    return rc == 0 ? 0 : 1;
}

static long long wrap_thrd_yield(void) {
    sched_yield();
    return 0;
}

static long long wrap_thrd_sleep(long long durp, long long remp) {
    if (!durp)
        return -1;
    VirtualMachine *vm = current_vm();
    ExecState       caller_state;
    if (vm)
        save_and_release_gil(vm, &caller_state);
    int rc = nanosleep((const struct timespec *)durp,
                       remp ? (struct timespec *)remp : NULL);
    if (vm)
        acquire_and_restore_gil(vm, &caller_state);
    return rc == 0 ? 0 : (errno == EINTR ? -1 : -2);
}

static long long wrap_thrd_current(void) {
    return wrap_pthread_self();
}

static long long wrap_thrd_equal(long long a, long long b) {
    return wrap_pthread_equal(a, b);
}

// mtx_t: wraps CCCCUserMutex with an extra type field.
// For mtx_recursive we need PTHREAD_MUTEX_RECURSIVE.
typedef struct {
    void *__handle;
    long  __state;
    int   __type;
} CCCCUserMtx;

static pthread_mutex_t *ensure_mtx(CCCCUserMtx *mtx) {
    if (!mtx)
        return NULL;
    if (mtx->__handle)
        return (pthread_mutex_t *)mtx->__handle;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if (mtx->__type == 1) // mtx_recursive
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_t *host = malloc(sizeof(*host));
    if (!host) {
        pthread_mutexattr_destroy(&attr);
        return NULL;
    }
    if (pthread_mutex_init(host, &attr) != 0) {
        pthread_mutexattr_destroy(&attr);
        free(host);
        return NULL;
    }
    pthread_mutexattr_destroy(&attr);
    mtx->__handle = host;
    mtx->__state  = 1;
    return host;
}

static long long wrap_mtx_init(long long mtxp, long long type) {
    CCCCUserMtx *mtx = (CCCCUserMtx *)mtxp;
    if (!mtx)
        return 1;
    if (mtx->__handle)
        return 1;
    mtx->__type           = (int)type;
    pthread_mutex_t *host = ensure_mtx(mtx);
    return host ? 0 : 1;
}

static long long wrap_mtx_lock(long long mtxp) {
    VirtualMachine  *vm   = current_vm();
    CCCCUserMtx     *mtx  = (CCCCUserMtx *)mtxp;
    pthread_mutex_t *host = ensure_mtx(mtx);
    if (!vm || !host)
        return 1;
    if (vm->flags & CCCC_THREAD_SAFETY) {
        ThreadRecord *tr = current_thread(vm);
        if (tr && check_lock_safety(vm, tr, (void *)mtxp))
            return EDEADLK;
    }
    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    int rc = pthread_mutex_lock(host);
    acquire_and_restore_gil(vm, &caller_state);
    if (rc == 0 && (vm->flags & CCCC_THREAD_SAFETY)) {
        ThreadRecord *tr = current_thread(vm);
        if (tr)
            thread_add_held_lock(tr, (void *)mtxp);
    }
    return rc == 0 ? 0 : 1;
}

static long long wrap_mtx_trylock(long long mtxp) {
    VirtualMachine  *vm   = current_vm();
    CCCCUserMtx     *mtx  = (CCCCUserMtx *)mtxp;
    pthread_mutex_t *host = ensure_mtx(mtx);
    if (!host)
        return 1;
    int rc = pthread_mutex_trylock(host);
    if (rc == EBUSY)
        return EBUSY;
    if (rc == 0 && vm && (vm->flags & CCCC_THREAD_SAFETY)) {
        ThreadRecord *tr = current_thread(vm);
        if (tr)
            thread_add_held_lock(tr, (void *)mtxp);
    }
    return rc == 0 ? 0 : 1;
}

static long long wrap_mtx_timedlock(long long mtxp, long long tsp) {
    VirtualMachine  *vm   = current_vm();
    CCCCUserMtx     *mtx  = (CCCCUserMtx *)mtxp;
    pthread_mutex_t *host = ensure_mtx(mtx);
    if (!vm || !host || !tsp)
        return 1;
    const struct timespec *abs_ts = (const struct timespec *)tsp;
    ExecState              caller_state;
    save_and_release_gil(vm, &caller_state);
    int rc;
#if defined(__linux__)
    rc = pthread_mutex_timedlock(host, abs_ts);
#else
    // macOS lacks pthread_mutex_timedlock; poll with exponential back-off.
    for (;;) {
        rc = pthread_mutex_trylock(host);
        if (rc == 0)
            break;
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > abs_ts->tv_sec ||
            (now.tv_sec == abs_ts->tv_sec && now.tv_nsec >= abs_ts->tv_nsec)) {
            rc = ETIMEDOUT;
            break;
        }
        struct timespec delay = {0, 1000000}; // 1 ms
        nanosleep(&delay, NULL);
    }
#endif
    acquire_and_restore_gil(vm, &caller_state);
    if (rc == ETIMEDOUT)
        return ETIMEDOUT;
    if (rc == 0 && (vm->flags & CCCC_THREAD_SAFETY)) {
        ThreadRecord *tr = current_thread(vm);
        if (tr)
            thread_add_held_lock(tr, (void *)mtxp);
    }
    return rc == 0 ? 0 : 1;
}

static long long wrap_mtx_unlock(long long mtxp) {
    VirtualMachine *vm  = current_vm();
    CCCCUserMtx    *mtx = (CCCCUserMtx *)mtxp;
    if (!mtx || !mtx->__handle)
        return 1;
    int rc = pthread_mutex_unlock((pthread_mutex_t *)mtx->__handle);
    if (rc == 0 && vm && (vm->flags & CCCC_THREAD_SAFETY)) {
        ThreadRecord *tr = current_thread(vm);
        if (tr)
            thread_remove_held_lock(tr, (void *)mtxp);
    }
    return rc == 0 ? 0 : 1;
}

static long long wrap_mtx_destroy(long long mtxp) {
    CCCCUserMtx *mtx = (CCCCUserMtx *)mtxp;
    if (!mtx || !mtx->__handle)
        return 0;
    pthread_mutex_destroy((pthread_mutex_t *)mtx->__handle);
    free(mtx->__handle);
    mtx->__handle = NULL;
    mtx->__state  = 0;
    return 0;
}

static long long wrap_cnd_init(long long condp) {
    return wrap_pthread_cond_init(condp, 0);
}

static long long wrap_cnd_wait(long long condp, long long mtxp) {
    // cnd_wait(cnd_t*, mtx_t*) — mtx_t aliases CCCCUserMutex at offset 0
    VirtualMachine  *vm    = current_vm();
    CCCCUserMtx     *mtx   = (CCCCUserMtx *)mtxp;
    pthread_cond_t  *cond  = ensure_cond((CCCCUserCond *)condp);
    pthread_mutex_t *mutex = ensure_mtx(mtx);
    if (!vm || !cond || !mutex)
        return 1;
    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    int rc = pthread_cond_wait(cond, mutex);
    acquire_and_restore_gil(vm, &caller_state);
    return rc == 0 ? 0 : 1;
}

static long long wrap_cnd_signal(long long condp) {
    return wrap_pthread_cond_signal(condp);
}

static long long wrap_cnd_broadcast(long long condp) {
    return wrap_pthread_cond_broadcast(condp);
}

static long long wrap_cnd_timedwait(long long condp, long long mtxp,
                                    long long tsp) {
    VirtualMachine  *vm    = current_vm();
    CCCCUserMtx     *mtx   = (CCCCUserMtx *)mtxp;
    pthread_cond_t  *cond  = ensure_cond((CCCCUserCond *)condp);
    pthread_mutex_t *mutex = ensure_mtx(mtx);
    if (!vm || !cond || !mutex || !tsp)
        return 1;
    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    int rc = pthread_cond_timedwait(cond, mutex, (const struct timespec *)tsp);
    acquire_and_restore_gil(vm, &caller_state);
    if (rc == ETIMEDOUT)
        return ETIMEDOUT;
    return rc == 0 ? 0 : 1;
}

static long long wrap_cnd_destroy(long long condp) {
    return wrap_pthread_cond_destroy(condp);
}

static long long wrap_tss_create(long long keyp, long long dtor) {
    return wrap_pthread_key_create(keyp, dtor);
}

static long long wrap_tss_get(long long key) {
    return wrap_pthread_getspecific(key);
}

static long long wrap_tss_set(long long key, long long val) {
    long long rc = wrap_pthread_setspecific(key, val);
    return rc == 0 ? 0 : 1;
}

static long long wrap_tss_delete(long long key) {
    return wrap_pthread_key_delete(key);
}

// call_once(once_flag *, void (*)(void)) -- #1088. Used to be a guest-side
// macro (see include/threads.h's own comment on the change); now a real cfunc
// so both backends share one race-free implementation instead of relying on
// the GIL. __atomic_compare_exchange_n makes exactly one caller, among any
// number racing on the same flag, observe the 0->1 transition and run func();
// every other caller either sees it already 1 (already run or in progress)
// or loses the race and returns without running func() itself. Unlike
// call_tss_destructor_top_level/drain_exit_handlers_nested this can't spin-
// wait for a concurrent initializer to finish (C11 doesn't require it to),
// so a caller that loses the race may return before func() completes on
// another thread -- matches the standard's own minimal guarantee (7.26.6.2p2:
// "on return from call_once the completion of the function pointed to by
// func synchronizes with all subsequent calls to call_once with the same
// flag" -- i.e. ordering only, not "func has already run by the time we
// return"). func is a guest function pointer, so it must be invoked through
// cccc_call_guest_callback -- this cfunc is itself running as a nested,
// mid-vm_eval FFI call (GIL held), exactly the context that requires,
// matching drain_exit_handlers_nested (stdlib.c) and run_tss_destructors
// above.
static long long wrap_call_once(long long flagp, long long func) {
    VirtualMachine *vm   = current_vm();
    int            *flag = (int *)flagp;
    if (!vm || !flag)
        return 0;
    int expected = 0;
    if (__atomic_compare_exchange_n(flag, &expected, 1, 0, __ATOMIC_SEQ_CST,
                                    __ATOMIC_SEQ_CST)) {
        long long ignored;
        cccc_call_guest_callback(vm, func, NULL, 0, &ignored);
    }
    return 0;
}

void register_threads_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "thrd_create", (void *)wrap_thrd_create, 3, 0);
    cc_register_cfunc(vm, "thrd_join", (void *)wrap_thrd_join, 2, 0);
    cc_register_cfunc(vm, "thrd_exit", (void *)wrap_thrd_exit, 1, 0);
    cc_register_cfunc(vm, "thrd_detach", (void *)wrap_thrd_detach, 1, 0);
    cc_register_cfunc(vm, "thrd_yield", (void *)wrap_thrd_yield, 0, 0);
    cc_register_cfunc(vm, "thrd_sleep", (void *)wrap_thrd_sleep, 2, 0);
    cc_register_cfunc(vm, "thrd_current", (void *)wrap_thrd_current, 0, 0);
    cc_register_cfunc(vm, "thrd_equal", (void *)wrap_thrd_equal, 2, 0);
    cc_register_cfunc(vm, "mtx_init", (void *)wrap_mtx_init, 2, 0);
    cc_register_cfunc(vm, "mtx_lock", (void *)wrap_mtx_lock, 1, 0);
    cc_register_cfunc(vm, "mtx_trylock", (void *)wrap_mtx_trylock, 1, 0);
    cc_register_cfunc(vm, "mtx_timedlock", (void *)wrap_mtx_timedlock, 2, 0);
    cc_register_cfunc(vm, "mtx_unlock", (void *)wrap_mtx_unlock, 1, 0);
    cc_register_cfunc(vm, "mtx_destroy", (void *)wrap_mtx_destroy, 1, 0);
    cc_register_cfunc(vm, "cnd_init", (void *)wrap_cnd_init, 1, 0);
    cc_register_cfunc(vm, "cnd_wait", (void *)wrap_cnd_wait, 2, 0);
    cc_register_cfunc(vm, "cnd_signal", (void *)wrap_cnd_signal, 1, 0);
    cc_register_cfunc(vm, "cnd_broadcast", (void *)wrap_cnd_broadcast, 1, 0);
    cc_register_cfunc(vm, "cnd_timedwait", (void *)wrap_cnd_timedwait, 3, 0);
    cc_register_cfunc(vm, "cnd_destroy", (void *)wrap_cnd_destroy, 1, 0);
    cc_register_cfunc(vm, "tss_create", (void *)wrap_tss_create, 2, 0);
    cc_register_cfunc(vm, "tss_get", (void *)wrap_tss_get, 1, 0);
    cc_register_cfunc(vm, "tss_set", (void *)wrap_tss_set, 2, 0);
    cc_register_cfunc(vm, "tss_delete", (void *)wrap_tss_delete, 1, 0);
    cc_register_cfunc(vm, "call_once", (void *)wrap_call_once, 2, 0);
}

void cccc_pthread_cleanup(VirtualMachine *vm) {
    if (!vm)
        return;
    ThreadRecord *main_thread =
        vm->pthread_state ? &vm->pthread_state->main_thread : NULL;
    ThreadRecord *rec = vm->thread_records;
    while (rec) {
        ThreadRecord *next = rec->next;
        if (rec != main_thread) {
            if (!rec->joined && !rec->detached)
                pthread_detach(rec->host_thread);
            free_thread_record(rec);
        }
        rec = next;
    }
    if (main_thread) {
        CCCCPthreadValue *value = main_thread->values;
        while (value) {
            CCCCPthreadValue *next = value->next;
            free(value);
            value = next;
        }
        main_thread->values = NULL;
        // The main thread's ThreadRecord is embedded in PthreadState (not
        // heap-allocated), so it never goes through free_thread_record --
        // free its host allocations here instead. tls_seg is always NULL for
        // main (its TLS copy lives in vm->current_tls_seg, freed separately
        // in vm.c), so held_locks is the only other field to reclaim.
        free(main_thread->held_locks);
        main_thread->held_locks       = NULL;
        main_thread->held_locks_count = 0;
        main_thread->held_locks_cap   = 0;
    }
    vm->thread_records    = NULL;

    PthreadKeyRecord *key = vm->pthread_keys;
    while (key) {
        PthreadKeyRecord *next = key->next;
        free(key);
        key = next;
    }
    vm->pthread_keys = NULL;

    free(vm->pthread_state);
    vm->pthread_state = NULL;

    free(vm->lock_graph_from);
    free(vm->lock_graph_to);
    vm->lock_graph_from = NULL;
    vm->lock_graph_to   = NULL;
    vm->lock_graph_size = 0;
    vm->lock_graph_cap  = 0;
}
#else
void register_pthread_functions(VirtualMachine *vm) {
    (void)vm;
}

void register_threads_functions(VirtualMachine *vm) {
    (void)vm;
}

void cccc_pthread_cleanup(VirtualMachine *vm) {
    (void)vm;
}

void cccc_pthread_run_main_tss_destructors(VirtualMachine *vm) {
    (void)vm;
}
#endif
