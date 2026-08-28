// CCCC_FLAGS: -Wexcess-init
// CCCC_EXPECT_STDERR: initializer-string for array is too long \(4 chars into 3 available\).*\[-Wexcess-init\]

int main(void) {
    char c[3] = "abcd"; // 4 chars + NUL don't fit in 3
    return c[0] == 'a' ? 42 : 1;
}
