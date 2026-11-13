// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: foo\.c:100:
// CCCC_REJECT_STDERR: foo\.c:101:
// `#line N "name"` renames the file and numbers the next line N.
int x;
#line 100 "foo.c"
int y = z;

int main(void) {
    return 0;
}
