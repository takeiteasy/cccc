// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked array local variable
// The pragma form of a region applies the array rule too.

#pragma cccc checked begin
int f(void) {
    int a[3] = {1, 2, 3};
    return a[0];
}
#pragma cccc checked end

int main(void) {
    return 42;
}
