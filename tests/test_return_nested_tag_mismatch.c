// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'struct B' does not match the type of the array element

struct A {
    int x;
};
struct B {
    int x;
};
struct Outer {
    struct A arr[2];
};

[[cccc::test(return = (struct Outer){.arr = {(struct B){.x = 1}}})]] struct Outer
    test_wrong_nested_tag(void) {
    return (struct Outer){{{1}, {2}}};
}
