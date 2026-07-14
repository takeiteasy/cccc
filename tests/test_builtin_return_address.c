// CCCC_FLAGS: --testing
// Tests for __builtin_return_address(0)
// Returns a void* (NULL sentinel stub); real return-address capture is a follow-up.

#include <stddef.h>

[[cccc::test]]
void test_return_address_is_ptr(void) {
    void *ra = __builtin_return_address(0);
    // Just ensure it compiles and returns a pointer-typed value.
    // The sentinel value is NULL, so just check the type is accepted.
    (void)ra;
}

[[cccc::test]]
void test_return_address_expr(void) {
    // Should be usable in pointer expressions
    void *p = (void *)__builtin_return_address(0);
    (void)(p == NULL || p != NULL); // both outcomes acceptable
}
