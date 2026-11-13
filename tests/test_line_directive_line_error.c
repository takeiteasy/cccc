// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: line_error\.c:100:
// CCCC_REJECT_STDERR: line_error\.c:101:
// `#line N` numbers the next line N and keeps the file name.
int x;
#line 100
int y = z;

int main(void) {
    return 0;
}
