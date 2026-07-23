// EXPECT_COMPILE_ERROR
// Vector-by-value through a *variadic* native FFI call (tracker #721):
// #721 lifts the internal-ABI variadic-vector restriction (see
// test_attr_vector_size_variadic.c), but a variadic call to an
// FFI-registered function (printf here) still goes through the FFI arg
// classification (codegen.c, the ffi_idx >= 0 branch), which is a separate
// guard from the internal-call-ABI ones #721 relaxes and is unaffected by
// this change. It must keep rejecting a vector argument -- see
// test_attr_vector_size_ffi_error.c and follow-up ticket #726 for real FFI
// vector-by-value support.

#include <stdio.h>

typedef float v4sf __attribute__((vector_size(16)));

int main(void) {
    v4sf a;
    a[0] = 1.0f;
    printf("%d\n", a);
    return 0;
}
