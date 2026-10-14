// A header ending in a conditional after its guard's #endif is not guarded
// whole, so a second #include still defines the implementation.
#include "test_include_guard_two_include.h"

#define LIB_IMPL
#include "test_include_guard_two_include.h"

int main(void) {
    return lib_run();
}
