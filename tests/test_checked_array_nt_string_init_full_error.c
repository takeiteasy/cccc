// EXPECT_COMPILE_ERROR
// A string initializer must leave room for the terminator slot.

int main(void) {
    char s _Nt_checked[3] = "abc";
    return s[0];
}
