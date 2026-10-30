// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: SLT3
// CCCC_REJECT_STDOUT: ADDI3[^:]*: ZX4
// An unsigned index below its bound stays in range after `i++`.
void mark(unsigned *p) {
    *p = 7;
}

void count_up(unsigned *a, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        mark(&a[i]);
}

int main(void) {
    unsigned a[3] = {0, 0, 0};
    count_up(a, 3);
    return a[0] == 7 && a[1] == 7 && a[2] == 7 ? 42 : 1;
}
