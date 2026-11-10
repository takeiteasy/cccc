// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a template enum needs a tag name
@template(T) enum { A };

int main(void) { return 42; }
