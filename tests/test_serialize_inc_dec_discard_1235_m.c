// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: j = \(
// CCCC_EXPECT_STDOUT: \+ -1
// CCCC_REJECT_STDOUT: ^[ \t]*for \([^\n]*(\+ -1\)|\+ -1\.0)
// CCCC_HOST_CC: -std=gnu11 -Wunused-value -Werror -Wno-unused-variable
// -m output drops the compensating term from for-update clauses and the host
// compiler reports no unused value.
int main(void) {
    int    a[4] = {0, 0, 0, 0};
    int    i;
    int   *p;
    double d;
    for (i = 0; i < 4; i++)
        a[i] = i + 1;
    for (p = a; p < a + 4; p++)
        *p += 10;
    for (d = 0.0; d < 3.0; d++)
        ;
    i--;
    p--;
    d--;
    int j = i++;
    int s = a[0] + a[1] + a[2] + a[3];
    return s + i + j + (int)d - (int)(p - a) - 14;
}
