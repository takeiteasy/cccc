// CCCC_FLAGS: -Wconversion
// CCCC_EXPECT_STDERR: \[-Wfloat-conversion\]

int main(void) {
    double d = 3.7;
    int    i = d; // float -> int: float-conversion
    return i == 3 ? 42 : 0;
}
