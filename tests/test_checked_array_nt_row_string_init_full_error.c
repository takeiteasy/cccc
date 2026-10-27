// EXPECT_COMPILE_ERROR
// A row's string initializer must leave room for the row's terminator slot.

int main(void) {
    char s _Nt_checked[2][3] = {"ab", "abc"};
    return s[0][0];
}
