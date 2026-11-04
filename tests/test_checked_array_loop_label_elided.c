// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_EXPECT_STDOUT: (CHKR .*){1}
// CCCC_REJECT_STDOUT: (CHKR .*){2}
//
// A label nothing jumps to does not stop a counted loop's index from being
// proven; a label a goto targets does.

int zero;

int untargeted(void) {
    int a _Checked[5] = {1, 2, 3, 4, 5};
    int   sum         = 0;
    for (int i = 0; i < 5; i++) {
    unused:
        sum += a[i];
    }
    return sum;
}

int targeted(void) {
    int a _Checked[5] = {1, 2, 3, 4, 5};
    int   sum         = 0;
    for (int i = 0; i < 5; i++) {
    again:
        sum += a[i];
        if (zero)
            goto again;
    }
    return sum;
}

int main(void) {
    return untargeted() + targeted() == 30 ? 42 : 1;
}
