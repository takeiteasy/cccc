// CCCC_FLAGS: -Wexcess-init
// CCCC_EXPECT_STDERR: excess elements in array initializer.*\[-Wexcess-init\]
// CCCC_REJECT_STDERR: (excess elements(.|\n)*){2}
//
// Pins once-per-list behavior (matching clang, not GCC's once-per-element):
// the inner list {1,2} is one element too long for int[1], so it warns
// exactly once, not twice.

int main(void) {
    int a[2][1] = {{1, 2}, {3}};
    return a[0][0] + a[1][0] + 38;
}
