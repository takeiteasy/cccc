// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: ^#define K 3 /\* spans +two lines \*/$
// CCCC_EXPECT_STDOUT: ^#define S "/\*"$
#define K 3 /* spans
   two lines */
#define S "/*"
int main(void) { return K + 39; }
