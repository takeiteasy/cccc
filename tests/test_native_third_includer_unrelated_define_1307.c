// CCCC_FLAGS: tests/fixtures/third_includer_1307_smoke_a.c tests/fixtures/third_includer_1307_smoke_b.c
// A third TU including the shared header after an unrelated #define builds
// under -c=native.
#if 1
#define C_UNRELATED_1307_SMOKE 1
#endif
#include "fixtures/config_1307_smoke.h"
int third_includer_1307_smoke_c_unused(void) {
    return C_UNRELATED_1307_SMOKE;
}
