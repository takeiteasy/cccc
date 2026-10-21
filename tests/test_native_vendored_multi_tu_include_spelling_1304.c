// CCCC_FLAGS: tests/fixtures/vendored_1304_smoke_a2.c
// A single-header library included as "./lib.h" by one TU and "lib.h" by the
// other, only one defining its IMPLEMENTATION macro, builds under -c=native.
#define VENDORED_1304_SMOKE2_IMPLEMENTATION
#include "fixtures/vendored_1304_smoke_lib2.h"
int vendored_1304_smoke2_call_a(void);
int main(void) {
    return vendored_1304_smoke2_call_a() + v1304_smoke2_add(20);
}
