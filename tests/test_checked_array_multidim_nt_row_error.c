// EXPECT_COMPILE_ERROR
// A checked array of `_Nt_checked` rows (through a typedef) is rejected the
// same way as the direct spelling.

typedef char Str _Nt_checked[10];

int main(void) {
    Str t _Checked[3];
    return t[0][0];
}
