// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: has no bounds in a checked region
// Checked regions: array-kind member with no bounds form.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]]
void f(void) {
    struct S {
        int *[[cccc::array]] p;
    };
    struct S s = {0};
    (void)s;
}

int main(void) {
    return 42;
}
