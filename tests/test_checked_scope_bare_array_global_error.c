// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: global variable 'g_arr' has no bounds in a checked region
// Checked regions: array-kind global with no bounds form.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

#pragma cccc checked begin
int *[[cccc::array]] g_arr;
#pragma cccc checked end

int main(void) {
    return 42;
}
