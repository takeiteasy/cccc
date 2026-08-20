// CCCC_FLAGS: -Wfloat-equal
// CCCC_EXPECT_STDERR: comparing floating-point values with
// ==.*\[-Wfloat-equal\]

int main(void) {
    double x = 1.0;
    double y = 1.0;
    (void)(x == y);
    return 42;
}
