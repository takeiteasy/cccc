// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: SLT3
// CCCC_REJECT_STDOUT: ADDI3[^:]*: SX4
// Counters of nested loops bounded by `<` stay in range after `++`, so neither
// increment needs a sign extension.
int hits(int n, int row, const int *cols) {
    int h = 0;
    for (int c = 0; c < n; c++) {
        for (int r = 0; r < row; r++) {
            if (cols[r] == c) {
                h = h | 1;
                break;
            }
        }
    }
    return h;
}

int main(void) {
    int cols[3] = {0, 2, 1};
    return hits(3, 3, cols) == 1 ? 42 : 1;
}
