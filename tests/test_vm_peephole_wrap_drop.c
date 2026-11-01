// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : ADDSX3
// CCCC_REJECT_STDOUT: : (SX4|ZX4)
// A 32-bit result that is only stored to a 4-byte object, wrapped again, or
// stored narrower needs no wrap of its own, and the wrap of an accumulator is
// fused into its addition.
int sink;

void store_int(int *p, int a, int b) {
    *p = a * b + 1;
}

void store_global(int a, int b) {
    sink = (a + b) * 3;
}

void store_short(short *p, int a, int b) {
    *p = (short)(a + b);
}

void store_unsigned(unsigned *p, unsigned a, unsigned b) {
    *p = a - b;
}

int accumulate(int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += i * 3 ^ (i >> 2);
    return s;
}

int main(void) {
    int      i = 0;
    unsigned u = 0;
    short    h = 0;
    store_int(&i, 3, 4);
    store_global(1, 2);
    store_short(&h, 5, 6);
    store_unsigned(&u, 5, 6);
    accumulate(10);
    return 42;
}
