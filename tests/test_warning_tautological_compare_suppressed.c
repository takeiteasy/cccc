// CCCC_FLAGS: -Wno-tautological-compare
// CCCC_REJECT_STDERR: tautological-compare

int main(void) {
    unsigned int u = 5;
    if (u >= 0)
        u = 1;
    int x = 3;
    if (x == x)
        x = 2;
    return 42;
}
