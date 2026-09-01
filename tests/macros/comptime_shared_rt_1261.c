// #1261 fixture: the runtime-only definitions for comptime_shared_rt_1261.h.
// Passed to cccc as a separate command-line input. rt_log calls into
// <stdio.h>, which is never routed to the comptime pass, so its body cannot
// compile there regardless of input order. The fix makes forwarding
// demand-driven, so a body nothing in the comptime program references is
// never forwarded and never fails.
#include "comptime_shared_rt_1261.h"

#include <stdio.h>

static int g_scratch[4] = {10, 20, 30, 40};

int rt_add(int a, int b) {
    return a + b;
}

int rt_touch(void) {
    return g_scratch[0] + g_scratch[3];
}

int rt_log(const char *msg) {
    fprintf(stderr, "rt: %s\n", msg);
    return 7;
}
