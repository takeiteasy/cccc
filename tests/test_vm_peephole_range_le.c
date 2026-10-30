// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: ADDI3[^:]*: SX4
// With `<=` the index can be INT_MAX, so `j++` still has to wrap to int.
void mark(int *p) {
    *p = 7;
}

void count_up(int *a, int lo, int hi) {
    for (int j = lo; j <= hi; j++)
        mark(&a[j]);
}

int main(void) {
    int a[3] = {0, 0, 0};
    count_up(a, 0, 2);
    return a[0] == 7 && a[1] == 7 && a[2] == 7 ? 42 : 1;
}
