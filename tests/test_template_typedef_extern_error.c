// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a template cannot be typedef or extern
typedef @template(T) struct S { T a; } S;

int main(void) { return 42; }
