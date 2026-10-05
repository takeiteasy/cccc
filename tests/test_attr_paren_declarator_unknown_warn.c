// An unknown attribute after a parenthesised declarator warns once and is
// otherwise ignored.
// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: ^[^\n]*warning: unknown attribute 'bogus' ignored[^\n]*\n[^\n]*\n1 warning generated

static int one(void) {
    return 41;
}

int (*fp)(void) __attribute__((bogus)) = one;

int main(void) {
    return fp() + 1;
}
