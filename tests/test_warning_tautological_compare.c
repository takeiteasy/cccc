// CCCC_FLAGS: -Wtautological-compare
// CCCC_EXPECT_STDERR: comparison of unsigned expression >= 0 is always true.*\[-Wtautological-compare\]

int main(void) {
    unsigned int u = 5;
    if (u >= 0)
        u = 1;
    return 42;
}
