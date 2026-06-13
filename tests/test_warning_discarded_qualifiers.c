// CCCC_FLAGS: -Wdiscarded-qualifiers
// CCCC_EXPECT_STDERR: \[-Wdiscarded-qualifiers\]

const char *get_const(void) {
    return "hello";
}

int main(void) {
    const char *p = "hello";
    char *q = p;
    char *r = get_const();
    (void)q;
    (void)r;
    return 42;
}
