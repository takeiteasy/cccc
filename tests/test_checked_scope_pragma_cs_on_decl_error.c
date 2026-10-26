// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked pointer global variable
// #pragma CHECKED_SCOPE on applies to the rest of the file.

#pragma CHECKED_SCOPE on
int *g;

int main(void) {
    return 42;
}
