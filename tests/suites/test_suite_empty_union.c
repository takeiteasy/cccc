// CCCC_FLAGS: --testing
// Consolidated suite: empty unions
// Source tests: test_edge_empty_union_varargs (moved here from
//   test_suite_misc.c, #1120)
//
// Skipped by the --native corpus entirely (NATIVE_SKIP_TESTS in
// tools/testing/__init__.py): passing a zero-sized union through varargs is
// an inherent VM-vs-host divergence with no mergeable behaviour. The VM's
// own varargs machinery consumes no argument slot for a 0-byte aggregate and
// formats it as 0 ("Let's count: 3 0 2 1"); the host ABI passes the empty
// aggregate through a register and printf reads garbage for the conversion
// (measured directly: clang -std=gnu23, second %d prints an address).
// CCCC accepts empty unions as an extension; *using* one as a variadic
// argument only has defined semantics under the VM. Not serializer-fixable
// -- see NATIVE.md "Serialized-output divergences".

#include <stdio.h>

// [from test_edge_empty_union_varargs]
// Empty union variables (global, local, array) compile and have size 0.
union {
} misc_empty_global = {};
union {
} misc_empty_global_arr[3] = {};

[[cccc::test(return = 42, expect_stdout = "Let's count: 3 0 2 1")]]
int test_edge_empty_union_varargs(void) {
    union {
    } local_empty = {};
    union {
    } var[100] = {};
    (void)misc_empty_global;
    (void)misc_empty_global_arr[0];
    (void)local_empty;
    printf("Let's count: %d %d %d %d\n", 3, var[42], 2, 1);
    return 42;
}
