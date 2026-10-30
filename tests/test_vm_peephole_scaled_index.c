// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: ADDI3 +[0-9]+ 20 0
// CCCC_REJECT_STDOUT: MULADDI3
// Advancing a pointer by a constant count adds the scaled byte offset in one
// instruction: no register holds the count.
int advance(int *p, int n) {
    int k = 5;
    for (int i = 0; i < n; i++)
        p += k;
    return *p;
}

int main(void) {
    int a[32];
    for (int i = 0; i < 32; i++)
        a[i] = i;
    return advance(a, 3) == 15 ? 42 : 1;
}
