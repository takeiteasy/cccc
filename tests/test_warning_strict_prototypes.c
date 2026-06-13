// CCCC_FLAGS: --std=c17 -Wstrict-prototypes
// CCCC_EXPECT_STDERR: function declaration is not a prototype
// CCCC_EXPECT_STDERR: 1 warning generated.
// In C17, int foo() triggers -Wstrict-prototypes because it is not a prototype.
int add();

int add(int a, int b) {
    return a + b;
}

int main(void) {
    return add(19, 23) == 42 ? 42 : 1;
}
