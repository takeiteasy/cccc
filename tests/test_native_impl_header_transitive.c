// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: static int get\(int n\);\n#include "native_impl_header_wrap.h"
// -m/-c=native: the implementation header is reached through another header,
// so the prototype is printed ahead of the #include that brings it in.
static int get(int n) {
    return 42 + n;
}

#define LIB_IMPL
#define LIB_GET(p) get(p)
#include "native_impl_header_wrap.h"

int main(void) {
    return lib_run();
}
