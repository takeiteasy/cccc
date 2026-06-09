// time.h stdlib function registration
#include "../cccc.h"

static long long wrap_timespec_get(long long ts, long long base) {
    if ((int)base != TIME_UTC)
        return 0;
#if defined(TIME_UTC)
    return (long long)timespec_get((struct timespec *)ts, (int)base);
#elif defined(CLOCK_REALTIME)
    return clock_gettime(CLOCK_REALTIME, (struct timespec *)ts) == 0 ? base : 0;
#else
    (void)ts;
    return 0;
#endif
}

// Register all time.h functions
void register_time_functions(CCCC *vm) {
    // Time retrieval
    cc_register_cfunc(vm, "clock", (void*)clock, 0, 0);
    cc_register_cfunc(vm, "time", (void*)time, 1, 0);
    cc_register_cfunc(vm, "timespec_get", (void*)wrap_timespec_get, 2, 0);

    // Time manipulation
    cc_register_cfunc(vm, "difftime", (void*)difftime, 2, 1);
    cc_register_cfunc(vm, "mktime", (void*)mktime, 1, 0);

    // Time conversion
    cc_register_cfunc(vm, "asctime", (void*)asctime, 1, 0);
    cc_register_cfunc(vm, "ctime", (void*)ctime, 1, 0);
    cc_register_cfunc(vm, "gmtime", (void*)gmtime, 1, 0);
    cc_register_cfunc(vm, "gmtime_r", (void*)gmtime_r, 2, 0);
    cc_register_cfunc(vm, "localtime", (void*)localtime, 1, 0);
    cc_register_cfunc(vm, "localtime_r", (void*)localtime_r, 2, 0);

    // Formatting
    cc_register_cfunc(vm, "strftime", (void*)strftime, 4, 0);
}
