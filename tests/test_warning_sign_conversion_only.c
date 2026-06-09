// CCCC_FLAGS: -Wsign-conversion
// CCCC_EXPECT_STDERR: \[-Wsign-conversion\]
// CCCC_REJECT_STDERR: \[-Wconversion\]

// When only -Wsign-conversion is requested (not -Wconversion), only the
// sign-conversion sub-category fires; integer narrowing stays quiet.
int main(void) {
    int x = -1;
    unsigned int y = x;  // sign-conversion: fires
    return (int)y == -1 ? 42 : 0;
}
