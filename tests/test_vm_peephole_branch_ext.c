// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: LDR_INDEX_B
// CCCC_REJECT_STDOUT: ZX1
// A byte loaded and only tested for zero needs no zero extension.
static unsigned char bytes[4] = {0, 1, 0, 3};

long count(const unsigned char *p, long n) {
    long c = 0;
    for (long i = 0; i < n; i++)
        if (p[i])
            c++;
    return c;
}

int main(void) {
    return count(bytes, 4) == 2 ? 42 : 1;
}
