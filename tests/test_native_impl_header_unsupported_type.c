// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -c=native
// CCCC_EXPECT_STDERR: used by replayed header .* but cannot be declared ahead of its #include
// An input static whose signature names an input struct cannot be declared
// ahead of the replayed #include.
struct P {
    int a;
};

static int get(struct P *p) { return p ? p->a : 42; }

#define LIB_IMPL
#define LIB_GET(p) get(p)
#include "native_impl_header_struct.h"

int main(void) { return lib_run(); }
