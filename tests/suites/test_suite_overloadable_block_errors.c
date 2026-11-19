// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: overloadable functions declared in a block that the rules reject.

#define OVL __attribute__((overloadable))

int OVL pick(int x) {
    return 1;
}
int OVL pick(double x) {
    return 2;
}

int plain(int x) {
    return 5;
}

[[cccc::test(error = "call to 'pick' is ambiguous")]]
void test_block_member_does_not_leak(void) {
    {
        int OVL pick(long);
    }
    pick(1L);
}

[[cccc::test(error = "must also have the 'overloadable' attribute")]]
void test_unmarked_block_redeclaration_of_a_member(void) {
    int pick(int);
}

[[cccc::test(error = "must not have the 'overloadable' attribute")]]
void test_marked_block_redeclaration_of_a_plain_function(void) {
    int OVL plain(int);
}

[[cccc::test(error = "no overloaded function 'pick' matches the target type")]]
void test_block_set_hides_the_outer_member_from_a_target(void) {
    int OVL pick(int);
    int OVL pick(long);
    int (*p)(double) = pick;
}

[[cccc::test(error = "reference to overloaded function 'pick'")]]
void test_block_set_has_no_type_of_its_own(void) {
    int OVL pick(int);
    int OVL pick(long);
    void *p = (void *)pick;
}
