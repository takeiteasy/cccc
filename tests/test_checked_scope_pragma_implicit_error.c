// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: implicit conversion from an unchecked pointer
// Checked regions: the implicit-conversion ban applies in a pragma region.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

#pragma cccc checked begin
int f(void) {
    int *[[cccc::single]] p = g_raw;
    return *p;
}
#pragma cccc checked end

int main(void) {
    return 42;
}
