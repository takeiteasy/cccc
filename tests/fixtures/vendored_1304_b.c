// TU B for tests/test_serialize_vendored_multi_tu_include_1304.c (#1304).
// Defines VENDORED_1304_IMPLEMENTATION and re-includes
// vendored_1304_lib.h -- the *same* #include spelling TU A (the test file
// itself, command-line input 0) already captured without the
// IMPLEMENTATION macro defined. Before the fix, push_emit_directive's
// identical-text dedup (src/preprocess.c) kept TU A's #include position
// permanently, so this TU's own `#define VENDORED_1304_IMPLEMENTATION`
// line -- captured after it -- never actually configured the header by the
// time it was replayed, and v1304_add()'s body never reached the host
// compiler's output at all ("undefined symbol" at link time, confirmed
// pre-fix).
#define VENDORED_1304_IMPLEMENTATION
#include "vendored_1304_lib.h"

int vendored_1304_call_b(void) {
    return v1304_add(20);
}

int vendored_1304_call_a(void);

int main(void) {
    return vendored_1304_call_a() + vendored_1304_call_b();
}
