static int __cccc_native_sched_get_priority_min(int policy) {
    return sched_get_priority_min(
        __cccc_native_guest_to_host_sched_policy(policy));
}
