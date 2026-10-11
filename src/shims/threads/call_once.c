void __cccc_call_once(__cccc_once_flag *flag, void (*func)(void)) {
    int *raw = (int *)flag;
    int expected = 0;
    if (__atomic_compare_exchange_n(raw, &expected, 1, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        func();
        __atomic_store_n(raw, 2, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(raw, __ATOMIC_ACQUIRE) != 2)
            sched_yield();
    }
}
