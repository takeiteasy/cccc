// Two equal case labels beyond 32 bits are still a duplicate.
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: duplicate case value '9223372036854775808'

int f(unsigned long c) {
    switch (c) {
        case 9223372036854775808UL:
            return 1;
        case 9223372036854775808UL:
            return 2;
        default:
            return 0;
    }
}

int main(void) {
    return 42;
}
