// #1267 fixture: the ordinary-runtime module. Passed to cccc as a separate
// command-line input; it #includes the shared header the plain way, so
// helper()'s body is forwarded into the comptime program on demand (#1243)
// while the same header's anonymous enum typedef is also present textually
// via the primary input's `#include @comptime`.
#include "comptime_anon_enum_1267.h"

int helper(int n) {
    return n + V_A; // V_A == 0
}
