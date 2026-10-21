// CCCC_FLAGS: tests/fixtures/vendored_1304_smoke_a.c
// A single-header library included identically by two TUs, only one defining
// its IMPLEMENTATION macro, builds under -c=native.
#define VENDORED_1304_SMOKE_IMPLEMENTATION
#include "fixtures/vendored_1304_smoke_lib.h"
int vendored_1304_smoke_call_a(void);
int main(void) {
    return vendored_1304_smoke_call_a() + v1304_smoke_add(20);
}
