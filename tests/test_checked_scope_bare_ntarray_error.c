// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: has no bounds in a checked region
// Checked regions: ntarray kind with no bounds form.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]]
int f(void) {
    char *[[cccc::ntarray]] s = "hi";
    return s[0];
}

int main(void) {
    return 42;
}
