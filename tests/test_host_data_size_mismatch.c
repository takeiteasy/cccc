// EXPECT_COMPILE_ERROR
// CCCC_NATIVE_SKIP: the host compiler accepts the redeclaration
// CCCC_EXPECT_STDERR: extern 'environ' is 64 bytes; the host object is 8
// A declaration larger than the host object is an error, not an overrun.
extern char environ[64];

int main(void) {
    return environ[0];
}
