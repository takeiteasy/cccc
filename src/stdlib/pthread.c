// GIL-backed POSIX pthread wrapper for CCCC VM code.
#include "../cccc.h"
#include "../internal.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <errno.h>
#include <pthread.h>
#include <string.h>

typedef struct CCCCUserMutex {
    void *handle;
    long state;
} CCCCUserMutex;

typedef struct CCCCUserCond {
    void *handle;
    long state;
} CCCCUserCond;

typedef struct CCCCUserAttr {
    size_t stack_size;
    void *stack_addr;
} CCCCUserAttr;

typedef struct CCCCPthreadValue {
    int key;
    void *value;
    struct CCCCPthreadValue *next;
} CCCCPthreadValue;

struct PthreadKeyRecord {
    int key;
    void (*destructor)(void *);
    int deleted;
    struct PthreadKeyRecord *next;
};

struct ThreadRecord {
    pthread_t host_thread;
    VirtualMachine *vm;
    ExecState exec;
    long long start_fn;
    void *arg;
    void *retval;
    int vm_rc;
    int exited;
    int detached;
    int joined;
    CCCCPthreadValue *values;
    struct ThreadRecord *next;
    // Lock-order tracking (CCCC_THREAD_SAFETY)
    void **held_locks;      // array of CCCCUserMutex* currently held
    int    held_locks_count;
    int    held_locks_cap;
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
    // Stack canaries are broken when any function has parameters: ENT3 pushes
    // the canary at bp[-1] which conflicts with the first param/local at offset
    // -1 (see assign_stack_offsets). Disable them when threading starts.
    // Follow-up: ticket #441 tracks fixing the canary frame layout.
    vm->flags &= ~CCCC_STACK_CANARIES;
}

static PthreadState *pthread_state(VirtualMachine *vm) {
    if (!vm->pthread_state) {
        vm->pthread_state = calloc(1, sizeof(PthreadState));
        if (!vm->pthread_state)
            return NULL;
        vm->pthread_state->main_thread.vm = vm;
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

static void acquire_and_restore_gil(VirtualMachine *vm, const ExecState *state) {
    cccc_gil_acquire(vm);
    cccc_exec_state_restore(vm, state);
}

static void link_thread(VirtualMachine *vm, ThreadRecord *rec) {
    rec->next = vm->thread_records;
    vm->thread_records = rec;
}

static void unlink_thread(VirtualMachine *vm, ThreadRecord *rec) {
    ThreadRecord **cur = &vm->thread_records;
    while (*cur) {
        if (*cur == rec) {
            *cur = rec->next;
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
    cccc_exec_state_release_stack(rec->vm, &rec->exec);
    free(rec);
}

// ---------------------------------------------------------------------------
// Thread-safety helpers (CCCC_THREAD_SAFETY)
// ---------------------------------------------------------------------------

int cccc_thread_held_lock_count(VirtualMachine *vm) {
    if (!vm || !vm->pthread_state)
        return 0;
    ThreadRecord *tr = vm->active_thread
                           ? vm->active_thread
                           : &vm->pthread_state->main_thread;
    return tr->held_locks_count;
}

static void thread_add_held_lock(ThreadRecord *tr, void *mutex) {
    if (tr->held_locks_count >= tr->held_locks_cap) {
        int new_cap = tr->held_locks_cap ? tr->held_locks_cap * 2 : 4;
        void **new_arr = realloc(tr->held_locks, (size_t)new_cap * sizeof(void *));
        if (!new_arr)
            return;
        tr->held_locks = new_arr;
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
        int new_cap = vm->lock_graph_cap ? vm->lock_graph_cap * 2 : 8;
        void **new_from = realloc(vm->lock_graph_from, (size_t)new_cap * sizeof(void *));
        void **new_to   = realloc(vm->lock_graph_to,   (size_t)new_cap * sizeof(void *));
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
static int check_lock_safety(VirtualMachine *vm, ThreadRecord *tr, void *mutex) {
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

static void *vm_thread_start(void *arg) {
    ThreadRecord *rec = arg;
    VirtualMachine *vm = rec->vm;

    cccc_gil_acquire(vm);
    ThreadRecord *saved_active = vm->active_thread;
    vm->active_thread = rec;
    cccc_exec_state_restore(vm, &rec->exec);
    uint32_t saved_flags = vm->flags;
    vm->flags &= ~CCCC_STACK_CANARIES;
    rec->vm_rc = vm_eval(vm);
    vm->flags = saved_flags;
    rec->retval = (void *)vm->regs[REG_A0];
    rec->exited = 1;
    cccc_exec_state_save(vm, &rec->exec);
    vm->active_thread = saved_active;

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
    rec->vm = vm;
    rec->start_fn = start_fn;
    rec->arg = (void *)arg;
    if (cccc_exec_state_alloc_stack(vm, &rec->exec) != 0) {
        free(rec);
        return EAGAIN;
    }
    cccc_exec_state_prepare_call(vm, &rec->exec, entry, arg);
    link_thread(vm, rec);

    pthread_attr_t host_attr;
    pthread_attr_t *host_attr_ptr = NULL;
    CCCCUserAttr *user_attr = (CCCCUserAttr *)attrp;
    if (user_attr && user_attr->stack_size) {
        if (pthread_attr_init(&host_attr) == 0) {
            pthread_attr_setstacksize(&host_attr, user_attr->stack_size);
            host_attr_ptr = &host_attr;
        }
    }

    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    int rc = pthread_create(&rec->host_thread, host_attr_ptr, vm_thread_start, rec);
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
    VirtualMachine *vm = current_vm();
    ThreadRecord *rec = (ThreadRecord *)thread;
    if (!vm || !rec || rec->joined || rec->detached)
        return EINVAL;

    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    void *retval = NULL;
    int rc = pthread_join(rec->host_thread, &retval);
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
    if (rec)
        rec->retval = (void *)retval;
    vm->regs[REG_A0] = retval;
    vm->pc = CCCC_INVALID_PC;
    return 0;
}

static long long wrap_pthread_self(void) {
    VirtualMachine *vm = current_vm();
    ThreadRecord *rec = vm ? current_thread(vm) : NULL;
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
        if (pthread_mutex_init(host, NULL) != 0) {
            free(host);
            return NULL;
        }
        mutex->handle = host;
        mutex->state = 1;
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
        cond->state = 1;
    }
    return (pthread_cond_t *)cond->handle;
}

static long long wrap_pthread_mutex_init(long long mutexp, long long attrp) {
    (void)attrp;
    CCCCUserMutex *mutex = (CCCCUserMutex *)mutexp;
    if (!mutex)
        return EINVAL;
    if (mutex->handle)
        return EBUSY;
    pthread_mutex_t *host = ensure_mutex(mutex);
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
        mutex->state = 0;
    }
    return rc;
}

static long long wrap_pthread_mutex_lock(long long mutexp) {
    VirtualMachine *vm = current_vm();
    pthread_mutex_t *host = ensure_mutex((CCCCUserMutex *)mutexp);
    if (!vm || !host)
        return EINVAL;
    if (vm->flags & CCCC_THREAD_SAFETY) {
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
    VirtualMachine *vm = current_vm();
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
    VirtualMachine *vm = current_vm();
    CCCCUserMutex *mutex = (CCCCUserMutex *)mutexp;
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
        cond->state = 0;
    }
    return rc;
}

static long long wrap_pthread_cond_wait(long long condp, long long mutexp) {
    VirtualMachine *vm = current_vm();
    pthread_cond_t *cond = ensure_cond((CCCCUserCond *)condp);
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
    VirtualMachine *vm = current_vm();
    pthread_cond_t *cond = ensure_cond((CCCCUserCond *)condp);
    pthread_mutex_t *mutex = ensure_mutex((CCCCUserMutex *)mutexp);
    if (!vm || !cond || !mutex || !abstimep)
        return EINVAL;
    ExecState caller_state;
    save_and_release_gil(vm, &caller_state);
    int rc = pthread_cond_timedwait(cond, mutex, (const struct timespec *)abstimep);
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
    rec->key = ++vm->pthread_next_key;
    rec->destructor = (void (*)(void *))destructor;
    rec->next = vm->pthread_keys;
    vm->pthread_keys = rec;
    *(unsigned int *)keyp = (unsigned int)rec->key;
    hashmap_put_int(&vm->init_state, keyp, (void *)1);
    return 0;
}

static long long wrap_pthread_key_delete(long long key) {
    VirtualMachine *vm = current_vm();
    PthreadKeyRecord *rec = vm ? find_key(vm, (int)key) : NULL;
    if (!rec || rec->deleted)
        return EINVAL;
    rec->deleted = 1;
    return 0;
}

static long long wrap_pthread_getspecific(long long key) {
    VirtualMachine *vm = current_vm();
    ThreadRecord *thread = vm ? current_thread(vm) : NULL;
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
    VirtualMachine *vm = current_vm();
    ThreadRecord *thread = vm ? current_thread(vm) : NULL;
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
    cur->key = (int)key;
    cur->value = (void *)value;
    cur->next = thread->values;
    thread->values = cur;
    return 0;
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

static long long wrap_pthread_attr_setstacksize(long long attrp, long long stacksize) {
    if (!attrp)
        return EINVAL;
    ((CCCCUserAttr *)attrp)->stack_size = (size_t)stacksize;
    return 0;
}

static long long wrap_pthread_attr_getstack(long long attrp, long long stackaddrp,
                                            long long stacksizep) {
    VirtualMachine *vm = current_vm();
    CCCCUserAttr *attr = (CCCCUserAttr *)attrp;
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
    cc_register_cfunc(vm, "pthread_mutex_init", (void *)wrap_pthread_mutex_init, 2, 0);
    cc_register_cfunc(vm, "pthread_mutex_destroy", (void *)wrap_pthread_mutex_destroy, 1, 0);
    cc_register_cfunc(vm, "pthread_mutex_lock", (void *)wrap_pthread_mutex_lock, 1, 0);
    cc_register_cfunc(vm, "pthread_mutex_trylock", (void *)wrap_pthread_mutex_trylock, 1, 0);
    cc_register_cfunc(vm, "pthread_mutex_unlock", (void *)wrap_pthread_mutex_unlock, 1, 0);
    cc_register_cfunc(vm, "pthread_cond_init", (void *)wrap_pthread_cond_init, 2, 0);
    cc_register_cfunc(vm, "pthread_cond_destroy", (void *)wrap_pthread_cond_destroy, 1, 0);
    cc_register_cfunc(vm, "pthread_cond_wait", (void *)wrap_pthread_cond_wait, 2, 0);
    cc_register_cfunc(vm, "pthread_cond_timedwait", (void *)wrap_pthread_cond_timedwait, 3, 0);
    cc_register_cfunc(vm, "pthread_cond_signal", (void *)wrap_pthread_cond_signal, 1, 0);
    cc_register_cfunc(vm, "pthread_cond_broadcast", (void *)wrap_pthread_cond_broadcast, 1, 0);
    cc_register_cfunc(vm, "pthread_key_create", (void *)wrap_pthread_key_create, 2, 0);
    cc_register_cfunc(vm, "pthread_key_delete", (void *)wrap_pthread_key_delete, 1, 0);
    cc_register_cfunc(vm, "pthread_getspecific", (void *)wrap_pthread_getspecific, 1, 0);
    cc_register_cfunc(vm, "pthread_setspecific", (void *)wrap_pthread_setspecific, 2, 0);
    cc_register_cfunc(vm, "pthread_attr_init", (void *)wrap_pthread_attr_init, 1, 0);
    cc_register_cfunc(vm, "pthread_attr_destroy", (void *)wrap_pthread_attr_destroy, 1, 0);
    cc_register_cfunc(vm, "pthread_attr_setstacksize", (void *)wrap_pthread_attr_setstacksize, 2, 0);
    cc_register_cfunc(vm, "pthread_attr_getstack", (void *)wrap_pthread_attr_getstack, 3, 0);
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
    }
    vm->thread_records = NULL;

    PthreadKeyRecord *key = vm->pthread_keys;
    while (key) {
        PthreadKeyRecord *next = key->next;
        free(key);
        key = next;
    }
    vm->pthread_keys = NULL;

    free(vm->pthread_state);
    vm->pthread_state = NULL;
}
#else
void register_pthread_functions(VirtualMachine *vm) {
    (void)vm;
}

void cccc_pthread_cleanup(VirtualMachine *vm) {
    (void)vm;
}
#endif
