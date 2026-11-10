// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of template 'V'
@template(T) struct V { T a; };
@template(T) struct V { T b; };

int main(void) { return 0; }
