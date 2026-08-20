// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers -O2
// CCCC_MATRIX_SKIP: depends on -O2 specifically; the matrix only ever runs -O0
// + one -f pass. #944's CHKAB is emitted from the ND_ASSIGN store path, same
// exposure to fusion as CHKNT. Proves the assignment-bounds-implication trap
// survives -O2.

int main(void) {
    int *[[cccc::array, cccc::count(4)]] p = (int[4]){1, 2, 3, 4};
    int *[[cccc::array, cccc::count(10)]] q;
    q              = p;
    volatile int i = 7;
    return q[i];
}
