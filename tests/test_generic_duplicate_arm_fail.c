// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: '_Generic' specifies two compatible types
//
// #1224: C23 6.7.11p2 -- no two generic associations may specify
// compatible types. gcc and clang both hard-error here; CCCC used to
// silently take the first matching arm.
int main(void) {
    int x = 0;
    return _Generic(x, int: 1, int: 2, default: 0);
}
