// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked pointer global variable 'g_arr'
// Checked regions: an array of unchecked pointers as a global.

#pragma cccc checked begin
int *g_arr[2][2];
#pragma cccc checked end

int main(void) {
    return 42;
}
