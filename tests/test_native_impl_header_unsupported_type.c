// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -c=native
// CCCC_EXPECT_STDERR: used by replayed header .* but cannot be declared ahead of its #include
// An input static whose signature names a typedef the very header being
// replayed declares cannot be declared ahead of that #include.
#include "native_impl_header_decl.h"

static int seen;
static void note(lib_t *t) {
    seen += t->v;
}

#define LIB_IMPL
#define LIB_LOG(t) note(t)
#include "native_impl_header_decl.h"

int main(void) {
    lib_t t = {21};
    return lib_run(&t) + seen;
}
