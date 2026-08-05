#ifndef COMPTIME_SHARED_COMPTIME_FN_890_H
#define COMPTIME_SHARED_COMPTIME_FN_890_H
// Fixture for #890 (see test_comptime_include_shared_comptime_fn_890.c for
// the #894 update to this comment): a header that is @shared-included (so
// its declarations are replayed into the comptime program by the @shared
// mechanism) and also defines its own [[cccc::comptime]] function.

typedef struct {
    int x;
} shared_plan_t;

static int shared_plan_value;

[[cccc::comptime]]
static void set_shared_plan(void) {
    shared_plan_value = 42;
}

#endif
