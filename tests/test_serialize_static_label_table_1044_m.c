// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: &&zero
// CCCC_REJECT_STDOUT: ^static const void \*__cccc_tab_0
// CCCC_REJECT_STDERR: unresolved relocation
// -m output resolves the label addresses and hoists the table out of the
// function.

static int dispatch(int idx) {
    static const void *tab[] = {&&zero, &&ten, &&twenty};
    goto              *tab[idx];
zero:
    return 0;
ten:
    return 10;
twenty:
    return 20;
}

int main(void) {
    return dispatch(0) + dispatch(1) + dispatch(2) + 12;
}
