// EXPECT_COMPILE_ERROR
// _Bool is not a valid underlying type for an enum
enum E : _Bool { A = 0, B = 1 };

int main(void) { return 42; }
