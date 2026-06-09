// CCCC_FLAGS: -Wfloat-conversion
// CCCC_EXPECT_STDERR: \[-Wfloat-conversion\]
// CCCC_REJECT_STDERR: \[-Wconversion\]

// When only -Wfloat-conversion is requested, float->int fires but integer
// narrowing stays quiet.
int narrow(int x) { return x; }

int main(void) {
    double d = 3.7;
    int i = d;  // float-conversion: fires
    return i == 3 ? 42 : 0;
}
