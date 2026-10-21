// CCCC_FLAGS: tests/fixtures/shared_header_1305_smoke_a.c
// A shared header included by two TUs with an unrelated #define in between
// builds under -c=native.
#include "fixtures/plain_config_1305_smoke.h"
int cfg_1305_smoke_helper(int x) {
    return x;
}
int shared_header_1305_smoke_call_a(void);
int main(void) {
    return shared_header_1305_smoke_call_a() + 34;
}
