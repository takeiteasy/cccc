// TU B for tests/test_serialize_shared_header_unrelated_define_1305.c
// (#1305). Includes plain_config_1305.h with the IDENTICAL spelling the
// test file (TU A, command-line input 0) already captured -- the push-time
// route into push_emit_directive()'s #1304 dedup fix. Unlike TU A, this TU
// never defines anything -- it only exercises whether the dedup wrongly
// treats TU A's own, unrelated VENDORED_1305_IMPLEMENTATION #define
// (captured for a different header entirely) as reason to relocate
// plain_config_1305.h's #include past TU A's own earlier use of it.
#include "plain_config_1305.h"

int cfg_1305_helper(int x) {
    return x;
}

int shared_header_1305_call_a(void);

int main(void) {
    return shared_header_1305_call_a() + 34;
}
