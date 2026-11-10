// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a typedef template declares one name
@template(T) typedef T *A, B;

int main(void) { return 42; }
