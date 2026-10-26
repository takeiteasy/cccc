// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --checked-pointers
// CCCC_EXPECT_STDERR: checked initializer needs 4 bytes but the source array has 3

// An ntarray count(3) also claims the terminator slot: "ab" has only 3 bytes.
char *[[cccc::ntarray, cccc::count(3)]] s = "ab";

int main(void) {
    return 42;
}
