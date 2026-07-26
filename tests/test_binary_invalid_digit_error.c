// EXPECT_COMPILE_ERROR
// Regression test for #776: an invalid digit for the constant's base
// (here, '2' in a binary constant) used to be silently accepted, parsing
// as the wrong value instead of being rejected. Now a hard compile
// error, matching gcc/clang.
int main(void) {
    int x = 0b12;
    return x;
}
