// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: duplicate 'default' case in '_Generic'
//
// #1224: C23 6.7.11p2 -- a generic selection has at most one `default`
// association. gcc and clang both hard-error here.
int main(void) {
    int x = 0;
    return _Generic(x, int: 1, default: 2, default: 0);
}
