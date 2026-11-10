// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: the tag in a typedef template must be omitted or match the typedef name
@template(T) typedef struct Other { T a; } Mine;

int main(void) { return 42; }
