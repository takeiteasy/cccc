// CCCC_FLAGS: -Wfloat-equal
// CCCC_EXPECT_STDERR: comparing floating-point values with !=.*\[-Wfloat-equal\]

int main(void) {
    float a = 0.1f;
    float b = 0.2f;
    (void)(a != b);
    return 42;
}
