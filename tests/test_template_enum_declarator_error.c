// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a template enum cannot declare variables
@template(T) enum E { A } e;

int main(void) { return 42; }
