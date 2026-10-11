static int __cccc_native_sched_get_priority_max(int policy) {
    return sched_get_priority_max(
        __cccc_native_guest_to_host_sched_policy(policy));
}
