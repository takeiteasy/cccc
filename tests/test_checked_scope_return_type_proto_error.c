// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked pointer return type of function 'f'
// Checked regions: a prototype returning an unchecked pointer in a pragma
// region.

#pragma cccc checked begin
int *f(void);
#pragma cccc checked end

int main(void) {
    return 42;
}
