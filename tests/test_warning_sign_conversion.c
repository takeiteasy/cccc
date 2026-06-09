// CCCC_FLAGS: -Wconversion
// CCCC_EXPECT_STDERR: \[-Wsign-conversion\]

int main(void) {
    int x = -1;
    unsigned int y = x;  // signed -> unsigned: sign-conversion
    return (int)y == -1 ? 42 : 0;
}
