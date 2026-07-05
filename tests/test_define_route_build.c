// Ticket #611: @build route on #define, #ifdef, #undef in build mode (active).
// CCCC_FLAGS: --build

// @build defines are applied in build mode.
#define @build BUILD_JOBS 8
// @test defines are NOT applied in build mode.
#define @test  TEST_ONLY  1

// @build #ifdef is true when defined; plain #endif closes it.
#ifdef @build BUILD_JOBS
#define HAVE_BUILD_JOBS 1
#endif

[[cccc::build]]
int build_main(void) {
    int result = 42;

#ifndef HAVE_BUILD_JOBS
    result = 1; // should not execute
#endif

    if (BUILD_JOBS != 8)
        result = 2;

    // @test define must be absent in build mode.
#ifdef TEST_ONLY
    result = 3;
#endif

    // @build #ifdef — body executes.
#ifdef @build BUILD_JOBS
    result = result; // stays 42
#endif

    // @test #ifdef — body must be skipped; #else runs.
#ifdef @test TEST_ONLY
    result = 4;
#else
    result = result; // stays 42
#endif

    return result;
}
