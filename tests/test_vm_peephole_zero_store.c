// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: STR_INDEX_B
// CCCC_REJECT_STDOUT: LI3 +[0-9]+ 0 0[^:]*: STR_INDEX_B
// A zero stored through a pointer is stored from the zero register: no
// register is loaded with 0 first.
void clear(char *a, int n) {
    for (int j = 0; j < n; j++)
        a[j] = 0;
}

int main(void) {
    char b[4] = {1, 2, 3, 4};
    clear(b, 4);
    return b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0 ? 42 : 1;
}
