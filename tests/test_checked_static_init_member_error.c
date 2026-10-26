// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --checked-pointers
// CCCC_EXPECT_STDERR: checked initializer needs 32 bytes but the source array has 8

// A constant file-scope initializer whose count outruns the array it points at.
struct S {
    int n;
    int *[[cccc::array, cccc::count(n)]] p;
};

int      buf[2];
struct S g = {8, buf};

int main(void) {
    return 42;
}
