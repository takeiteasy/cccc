// TU B for tests/test_serialize_spelling_dedup_unrelated_define_1306.c
// (#1306). Includes config_1306.h under a DIFFERENT spelling ("config_1306.h",
// relative to this file's own directory) than the test file (TU A,
// command-line input 0) uses ("fixtures/config_1306.h", relative to
// tests/) -- both resolve to the same on-disk file, routing the dedup
// through cc_serialize_program()'s own #1292 canonical-path mechanism
// rather than push_emit_directive()'s identical-text dedup (#1305's own
// route, tests/test_serialize_shared_header_unrelated_define_1305.c).
#include "config_1306.h"

int cfg_1306_helper(int x) {
    return x;
}

int spelling_dedup_1306_call_a(void);

int main(void) {
    return spelling_dedup_1306_call_a() + 34;
}
