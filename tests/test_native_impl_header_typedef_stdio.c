// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: static int get\(size_t n\);\n#include "native_impl_header_struct.h"
// -m/-c=native: a standard typedef (size_t) reached only through another
// standard header is declared ahead of the implementation header's #include
// (the smoke test runs it without -I, where cccc prints <stddef.h> itself).
#include <stdio.h>

static int get(size_t n) {
    return 42 + (int)n;
}

#define LIB_IMPL
#define LIB_GET(p) get(p)
#include "native_impl_header_struct.h"

int main(void) {
    return lib_run();
}
