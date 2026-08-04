#ifndef COMPTIME_SHARED_COMPTIME_FN_890_H
#define COMPTIME_SHARED_COMPTIME_FN_890_H
// Fixture for #890: a header that is BOTH @shared-included (so its
// declarations are already replayed into the comptime program by the
// @shared mechanism) AND defines its own [[cccc::comptime]] function (so
// #890's fix also puts it in the allowed set and forwards its declarations
// a second time via the snapshot). The two forwarding paths must not
// collide — repeated typedefs/tentative defs are legal C.

typedef struct {
    int x;
} shared_plan_t;

static int shared_plan_value;

[[cccc::comptime]]
static void set_shared_plan(void) {
    shared_plan_value = 42;
}

#endif
