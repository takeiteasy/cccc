// CCCC_FLAGS: tests/fixtures/spelling_dedup_1306_smoke_a.c
// A header spelled two ways by two TUs, with an unrelated #define in between,
// builds under -c=native.
#include "fixtures/../fixtures/config_1306_smoke.h"
int cfg_1306_smoke_helper(int x) {
    return x;
}
int spelling_dedup_1306_smoke_call_a(void);
int main(void) {
    return spelling_dedup_1306_smoke_call_a() + 34;
}
