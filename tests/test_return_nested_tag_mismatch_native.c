// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing=native
// CCCC_EXPECT_STDERR: 'struct B' does not match the type of member 'in'

struct A {
    int x;
};
struct B {
    int x;
};
struct Outer {
    struct A in;
};

[[cccc::test(return = (struct Outer){.in = (struct B){.x = 1}})]] struct Outer
    test_wrong_nested_tag(void) {
    return (struct Outer){{1}};
}
