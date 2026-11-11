// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: int \* +q;
// CCCC_REJECT_STDOUT: ^ +q;
// CCCC_EXPECT_STDOUT: int \* +\* +p;
int * @single q;
int * @single * @array(4) p;
int main(void) {
    return 42;
}
