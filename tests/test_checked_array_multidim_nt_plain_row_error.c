// EXPECT_COMPILE_ERROR
// An `_Nt_checked` outer dimension over `_Checked` typedef rows is rejected:
// it would turn the typedef's last element into a terminator slot.

typedef char Row _Checked[10];

int main(void) {
    Row t _Nt_checked[3];
    return t[0][0];
}
