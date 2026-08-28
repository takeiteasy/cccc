// CCCC_FLAGS: -Wall
// CCCC_EXPECT_STDERR: excess elements in array initializer.*\[-Wexcess-init\]

int main(void) {
    int a[1] = {1, 2, 3};
    return a[0] + 41;
}
