// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: SLT3
// CCCC_REJECT_STDOUT: ADDI3[^:]*: SX4
// A loop index compared against a bound stays in int range after `j++` / `j--`
// on the branch that tested it, also across a call in the body.
void mark(int *p) {
    *p = 7;
}

void count_up(int *a, int lo, int hi) {
    for (int j = lo; j < hi; j++)
        mark(&a[j]);
}

void count_down(int *a, int lo, int hi) {
    for (int j = hi; j > lo; j--)
        mark(&a[j]);
}

int main(void) {
    int a[4] = {0, 0, 0, 0};
    count_up(a, 1, 3);
    count_down(a, 2, 3);
    return a[0] == 0 && a[1] == 7 && a[2] == 7 && a[3] == 7 ? 42 : 1;
}
