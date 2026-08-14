// posix_mqueue.c -- mqueue.h (Linux-only) (#946 split of posix.c).
#include "posix_util.h"

#if !defined(_WIN32) && !defined(_WIN64)

#ifdef __linux__
// mqueue.h (#805) -- Linux-only (see include/mqueue.h). mq_send/
// mq_receive/mq_timedsend/mq_timedreceive block on a full/empty queue, so
// they release the GIL; mq_open/mq_close/mq_unlink/mq_notify/mq_setattr/
// mq_getattr return promptly and keep it. mq_notify()'s SIGEV_THREAD is
// honored the same way aio's is, via cccc_posix_sigevent_prepare() above (struct
// sigevent has the same layout regardless of which header pulled it in).
static long long wrap_mq_open(const char *name, long long oflag, ...) {
    mqd_t r;
    if ((int)oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode_t mode = (mode_t)(unsigned int)va_arg(ap, unsigned int);
        struct mq_attr *attr = va_arg(ap, struct mq_attr *);
        va_end(ap);
        r = mq_open(name, (int)oflag, mode, attr);
    } else {
        r = mq_open(name, (int)oflag);
    }
    return (long long)r;
}

static long long wrap_mq_close(long long mqdes) {
    return (long long)mq_close((mqd_t)mqdes);
}

static long long wrap_mq_unlink(long long name) {
    return (long long)mq_unlink((const char *)(intptr_t)name);
}

static long long wrap_mq_send(long long mqdes, long long msg_ptr, long long msg_len, long long msg_prio) {
    VirtualMachine *vm = cccc_posix_current_vm();
    const char *p = (const char *)(intptr_t)msg_ptr;
    if (!vm || !vm->gil_initialized)
        return (long long)mq_send((mqd_t)mqdes, p, (size_t)msg_len, (unsigned int)msg_prio);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = mq_send((mqd_t)mqdes, p, (size_t)msg_len, (unsigned int)msg_prio);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_mq_receive(long long mqdes, long long msg_ptr, long long msg_len, long long msg_prio) {
    VirtualMachine *vm = cccc_posix_current_vm();
    char *p = (char *)(intptr_t)msg_ptr;
    unsigned int *prio = (unsigned int *)(intptr_t)msg_prio;
    if (!vm || !vm->gil_initialized)
        return (long long)mq_receive((mqd_t)mqdes, p, (size_t)msg_len, prio);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = mq_receive((mqd_t)mqdes, p, (size_t)msg_len, prio);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_mq_timedsend(long long mqdes, long long msg_ptr, long long msg_len,
                                   long long msg_prio, long long abs_timeout) {
    VirtualMachine *vm = cccc_posix_current_vm();
    const char *p = (const char *)(intptr_t)msg_ptr;
    const struct timespec *ts = (const struct timespec *)(intptr_t)abs_timeout;
    if (!vm || !vm->gil_initialized)
        return (long long)mq_timedsend((mqd_t)mqdes, p, (size_t)msg_len, (unsigned int)msg_prio, ts);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    int r = mq_timedsend((mqd_t)mqdes, p, (size_t)msg_len, (unsigned int)msg_prio, ts);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

static long long wrap_mq_timedreceive(long long mqdes, long long msg_ptr, long long msg_len,
                                      long long msg_prio, long long abs_timeout) {
    VirtualMachine *vm = cccc_posix_current_vm();
    char *p = (char *)(intptr_t)msg_ptr;
    unsigned int *prio = (unsigned int *)(intptr_t)msg_prio;
    const struct timespec *ts = (const struct timespec *)(intptr_t)abs_timeout;
    if (!vm || !vm->gil_initialized)
        return (long long)mq_timedreceive((mqd_t)mqdes, p, (size_t)msg_len, prio, ts);
    ExecState state;
    cccc_posix_save_and_release_gil(vm, &state);
    ssize_t r = mq_timedreceive((mqd_t)mqdes, p, (size_t)msg_len, prio, ts);
    cccc_posix_acquire_and_restore_gil(vm, &state);
    return (long long)r;
}

// mq_notify() registers at most one outstanding notification per queue and
// NULL deregisters it (POSIX); track which SIGEV_THREAD cookie (if any) is
// bound to a given mqdes so deregistration -- or replacement by a later
// mq_notify() call -- frees it instead of leaking a slot in
// g_sigev_cookies.
typedef struct {
    int in_use;
    mqd_t mqdes;
    int cookie_idx;
} MqNotifyBinding;
static MqNotifyBinding g_mq_notify_bindings[CCCC_SIGEV_MAX];

static void mq_notify_binding_clear(mqd_t mqdes) {
    for (int i = 0; i < CCCC_SIGEV_MAX; i++) {
        if (g_mq_notify_bindings[i].in_use && g_mq_notify_bindings[i].mqdes == mqdes) {
            cccc_sigev_cookie_free(g_mq_notify_bindings[i].cookie_idx);
            g_mq_notify_bindings[i].in_use = 0;
        }
    }
}

static void mq_notify_binding_set(mqd_t mqdes, int cookie_idx) {
    mq_notify_binding_clear(mqdes);
    for (int i = 0; i < CCCC_SIGEV_MAX; i++) {
        if (!g_mq_notify_bindings[i].in_use) {
            g_mq_notify_bindings[i].in_use = 1;
            g_mq_notify_bindings[i].mqdes = mqdes;
            g_mq_notify_bindings[i].cookie_idx = cookie_idx;
            return;
        }
    }
}

static long long wrap_mq_notify(long long mqdes, long long notification) {
    struct sigevent *sev = (struct sigevent *)(intptr_t)notification;
    if (!sev) {
        mq_notify_binding_clear((mqd_t)mqdes);
        return (long long)mq_notify((mqd_t)mqdes, NULL);
    }
    int is_thread = (sev->sigev_notify == SIGEV_THREAD);
    if (!cccc_posix_sigevent_prepare(sev))
        return -1;
    int cookie_idx = is_thread ? sev->sigev_value.sival_int : -1;
    int r = mq_notify((mqd_t)mqdes, sev);
    if (r != 0) {
        if (cookie_idx >= 0)
            cccc_sigev_cookie_free(cookie_idx);
        return (long long)r;
    }
    if (cookie_idx >= 0)
        mq_notify_binding_set((mqd_t)mqdes, cookie_idx);
    else
        mq_notify_binding_clear((mqd_t)mqdes); // SIGEV_NONE/SIGEV_SIGNAL replacing a prior THREAD registration
    return 0;
}

static long long wrap_mq_setattr(long long mqdes, long long newattr, long long oldattr) {
    return (long long)mq_setattr((mqd_t)mqdes, (const struct mq_attr *)(intptr_t)newattr,
                                 (struct mq_attr *)(intptr_t)oldattr);
}

static long long wrap_mq_getattr(long long mqdes, long long attr) {
    return (long long)mq_getattr((mqd_t)mqdes, (struct mq_attr *)(intptr_t)attr);
}
#endif

void register_posix_mqueue_functions(VirtualMachine *vm) {
#ifdef __linux__
    // mqueue.h (#805) -- Linux-only, see include/mqueue.h
    cc_register_variadic_cfunc(vm, "mq_open", (void*)wrap_mq_open, 2, 0);
    cc_register_cfunc(vm, "mq_close",         (void*)wrap_mq_close,         1, 0);
    cc_register_cfunc(vm, "mq_unlink",        (void*)wrap_mq_unlink,        1, 0);
    cc_register_cfunc(vm, "mq_send",          (void*)wrap_mq_send,          4, 0);
    cc_register_cfunc(vm, "mq_receive",       (void*)wrap_mq_receive,       4, 0);
    cc_register_cfunc(vm, "mq_timedsend",     (void*)wrap_mq_timedsend,     5, 0);
    cc_register_cfunc(vm, "mq_timedreceive",  (void*)wrap_mq_timedreceive,  5, 0);
    cc_register_cfunc(vm, "mq_notify",        (void*)wrap_mq_notify,        2, 0);
    cc_register_cfunc(vm, "mq_setattr",       (void*)wrap_mq_setattr,       3, 0);
    cc_register_cfunc(vm, "mq_getattr",       (void*)wrap_mq_getattr,       2, 0);
#else
    (void)vm;
#endif
}

#else
void register_posix_mqueue_functions(VirtualMachine *vm) { (void)vm; }
#endif
