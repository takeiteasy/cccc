// CCCC_FLAGS: --testing -F./tests/fixtures/frameworks
// CCCC_NATIVE_SKIP: framework lookup is Apple-toolchain only (gcc has no -F)
#include <Outer/Outer.h>
#include <Outer/Priv.h>

#if !__has_include(<Outer/Outer.h>)
#error "framework header not visible to __has_include"
#endif
#if __has_include(<Nope/Nope.h>)
#error "missing framework header reported present"
#endif

[[cccc::test(return = 42)]]
int test_framework_headers(void) {
    // Outer.h reaches Inner.h through Outer.framework/Frameworks.
    return OUTER_VALUE + PRIV_VALUE;
}
