// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A compound literal of checked array type is checked against its extent.

int main(void) {
    volatile int i = 4;
    return (int _Checked[4]){1, 2, 3, 4}[i];
}
