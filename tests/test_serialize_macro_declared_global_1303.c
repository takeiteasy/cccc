// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int g1303_thing = 21;
//
// #1303 (global_is_header_supplied() counterpart to #1301's function fix):
// a captured header's macro that *produces* a global definition when
// invoked, rather than writing the global itself --
// fixtures/macro_declared_global_1303_shared.h's V1303_DEFINE_G(n) expands
// to `int g1303_##n = 21;`. Invoked here (a command-line-input file) as
// V1303_DEFINE_G(thing), the resulting name's *spelling* location is the
// header (where the macro is defined), but its *expansion* location --
// what global_is_header_supplied() must actually key its decision on -- is
// this file. Getting this wrong reintroduces #1301's own pthread_once-class
// failure for globals: g1303_thing would be silently dropped as "already
// header-supplied" (both the #918 forward declaration and the definition),
// an "undeclared identifier" at this file's own use site. Confirmed via a
// standalone repro before global_is_header_supplied() walked
// Token.origin's expansion site the same way function_is_header_supplied()
// already did.
#include "fixtures/macro_declared_global_1303_shared.h"

V1303_DEFINE_G(thing)

int g1303_get(void) {
    return g1303_thing * 2;
}

int main(void) {
    return g1303_get();
}
