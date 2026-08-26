// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: alignment must be a power of two

// A malformed #pragma pack argument (neither a valid N, nor push/pop/()) is
// a hard error rather than a silent fallthrough to "unknown pragma ignored"
// -- that fallthrough is exactly the pre-#1173 bug, just under a narrower
// spelling.

#pragma pack(garbage)

int main(void) {
    return 0;
}
