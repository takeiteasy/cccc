// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: enum Color \{.*typedef int my_t;.*static my_t by_alias\(my_t k\);.*struct P \{.*struct P;\nstatic int by_ptr\(struct P \*p\);\n#include "native_impl_header_types.h"
// CCCC_REJECT_STDOUT: #include "native_impl_header_types.h".*(enum Color \{|typedef int my_t;|struct P \{)
// -m/-c=native: an includer function a replayed implementation-style header
// calls, whose signature names the includer's own types, gets those types
// declared ahead of the #include.
struct P {
    int a;
};
typedef int my_t;
enum Color { RED = 10 };

static struct P g_p = {10};

static int by_ptr(struct P *p) {
    return p->a;
}
static int by_val(struct P p) {
    return p.a;
}
static my_t by_alias(my_t k) {
    return k;
}
static int by_enum(enum Color c) {
    return c;
}

#define LIB_IMPL
#define LIB_A() by_ptr(&g_p)
#define LIB_B() by_val(g_p)
#define LIB_C() by_alias(12)
#define LIB_D() by_enum(RED)
#include "native_impl_header_types.h"

int main(void) {
    return lib_run();
}
