// EXPECT_COMPILE_ERROR
// An `_Nt_checked` outer dimension over rows that are already `_Nt_checked`
// is rejected: the terminator belongs to the innermost row, once.

typedef char Str _Nt_checked[10];

int main(void) {
    Str t _Nt_checked[3];
    return t[0][0];
}
