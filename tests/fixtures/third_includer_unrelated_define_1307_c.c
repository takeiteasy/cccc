// TU C for tests/test_serialize_third_includer_unrelated_define_1307.c
// (#1307). A THIRD includer of plain_config_1307.h -- mirrors
// src/stdlib/stdio.c's own real shape: a wholly unrelated #define,
// captured inside a conditional-group shell (#1064) with the shell's own
// #endif as the LAST directive captured before this TU's #include, not
// the #define itself. #1305's own scan (scoped to "any #define/#undef
// this TU captured ahead of its own #include") still wrongly saw
// C_UNRELATED_1307's #define and relocated plain_config_1307.h's #include
// to THIS TU's position -- later than the test file's own need for it.
// #1307's fix narrows the check to "the directive CAPTURED IMMEDIATELY
// BEFORE this occurrence", which correctly sees the #endif shell instead
// and does not relocate.
#if 1
#define C_UNRELATED_1307 1
#endif
#include "plain_config_1307.h"

int third_includer_1307_c_unused(void) {
    return C_UNRELATED_1307;
}
