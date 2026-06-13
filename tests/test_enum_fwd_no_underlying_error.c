// EXPECT_COMPILE_ERROR
// Forward declaration without underlying type is not allowed in C23
enum NoBase;

int main(void) { return 42; }
