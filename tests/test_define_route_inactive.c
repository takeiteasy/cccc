// Ticket #611: @build/@test route on #define, #ifdef, etc. in comp mode (inactive).
// Both routes are inactive so all mode-gated defines must be absent, and
// conditional nesting must stay balanced (plain #endif closes @build #ifdef).
#include <stdlib.h>

// These defines must NOT be applied in comp mode.
#define @build BUILD_CANARY 1
#define @test  TEST_CANARY  1

int main(void) {
    int result = 42;

    // The defines must be absent.
#ifdef BUILD_CANARY
    result = 1;
#endif
#ifdef TEST_CANARY
    result = 2;
#endif

    // @build #ifdef with plain #endif: must stay balanced and body skipped.
#ifdef @build BUILD_CANARY
    result = 3;
#endif

    // @test #ifdef with plain #endif: same.
#ifdef @test TEST_CANARY
    result = 4;
#endif

    // Inactive @build #ifdef — #else branch MUST run (desugars to #if 0).
#ifdef @build BUILD_CANARY
    result = 5;
#else
    result = result; // stays 42
#endif

    // Nested: outer @build false, inner @test also false; nesting must balance.
#ifdef @build BUILD_CANARY
#  ifdef @test TEST_CANARY
    result = 6;
#  endif
#endif

    return result; // must be 42
}
