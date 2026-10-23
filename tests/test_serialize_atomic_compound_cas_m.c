// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __atomic_compare_exchange_n
// CCCC_REJECT_STDOUT: __atomic_(add|sub|and|or|xor)_fetch
// -m output keeps the compare-exchange loop for an operator or operand the
// fetch-and-op builtins do not cover.
int main(void) {
    _Atomic int x   = 5;
    x              *= 3;
    x             <<= 1;
    x              += 1.5;
    return x == 31 ? 42 : 1;
}
