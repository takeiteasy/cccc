// CCCC_FLAGS: -Wunused
// CCCC_EXPECT_STDERR: unused function 'dead_fn'
// CCCC_EXPECT_STDERR: unused variable 'dead_var'
// CCCC_EXPECT_STDERR: unused function 'redecl'
// CCCC_REJECT_STDERR: 'used_later'
// CCCC_REJECT_STDERR: unused function 'redecl'[\s\S]*unused function 'redecl'
// An unused static is reported once, however often it is redeclared, and a
// static used through any declaration is not reported.

static int dead_fn(void) {
    return 1;
}
static int dead_var = 3;
static int used_later(void);

static int redecl(void);
static int redecl(void) {
    return 0;
}

int main(void) {
    return used_later() + 41;
}

static int used_later(void) {
    return 1;
}
