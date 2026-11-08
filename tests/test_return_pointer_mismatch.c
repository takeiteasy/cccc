// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDOUT: not ok 1 - test_null_field_but_pointer
// CCCC_EXPECT_STDOUT: not ok 2 - test_non_null_field_but_null
// CCCC_EXPECT_STDOUT: not ok 3 - test_address_constant
// CCCC_EXPECT_STDOUT: not ok 4 - test_field_ne_but_equal
// CCCC_EXPECT_STDERR: expected return value = \(struct Node\)\{\.next = NULL\}
// CCCC_EXPECT_STDERR: expected return value = \(struct Node\)\{\.next != NULL\}
// CCCC_EXPECT_STDERR: only NULL can be asserted for a pointer
// CCCC_EXPECT_STDERR: expected return value = \(struct Node\)\{\.val != 1\}

#include <stddef.h>

struct Node {
    int          val;
    struct Node *next;
};

static struct Node other;

[[cccc::test(return = (struct Node){.next = NULL})]] struct Node
    test_null_field_but_pointer(void) {
    return (struct Node){1, &other};
}

[[cccc::test(return = (struct Node){.next != NULL})]] struct Node
    test_non_null_field_but_null(void) {
    return (struct Node){1, NULL};
}

[[cccc::test(return = (struct Node){.next = 5})]] struct Node
    test_address_constant(void) {
    return (struct Node){1, NULL};
}

[[cccc::test(return = (struct Node){.val != 1})]] struct Node
    test_field_ne_but_equal(void) {
    return (struct Node){1, NULL};
}
