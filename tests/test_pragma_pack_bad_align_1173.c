// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: alignment must be a power of two

// #pragma pack(N) only accepts a power-of-two alignment in [1,16], matching
// GCC/MSVC -- a non-power-of-two must be a hard error, not silently rounded
// or ignored (#1173: the "accepted, not honoured" bug class this exists to
// close).

#pragma pack(3)
struct BadPackAlign1173 {
    char c;
    int  a;
};

int main(void) {
    return 0;
}
