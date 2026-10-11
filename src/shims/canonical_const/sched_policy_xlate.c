static int __cccc_native_guest_to_host_sched_policy(int guest_policy) {
    switch (guest_policy) {
        case 0:
            return SCHED_OTHER;
        case 1:
            return SCHED_FIFO;
        case 2:
            return SCHED_RR;
#ifdef __linux__
        case 3:
            return SCHED_BATCH;
        case 5:
            return SCHED_IDLE;
#endif
        default:
            return guest_policy;
    }
}
