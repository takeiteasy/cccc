// CCCC_FLAGS: -Wstatic-array-size
// CCCC_REJECT_STDERR: \[-Wstatic-array-size\]
void f(int a[static 10]) {}
int main(void) {
    int exact[10];
    int big[20];
    f(exact);
    f(big);
    return 42;
}
