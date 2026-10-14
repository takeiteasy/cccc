// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: #include <stddef.h>\n.*static int get\(size_t n\);\n#include "native_impl_header_struct.h"
// -m/-c=native: an includer function taking a typedef an earlier replayed
// header supplies is declared ahead of the implementation header's #include.
#include <stddef.h>

static int get(size_t n) {
    return 42 + (int)n;
}

#define LIB_IMPL
#define LIB_GET(p) get(p)
#include "native_impl_header_struct.h"

int main(void) {
    return lib_run();
}
