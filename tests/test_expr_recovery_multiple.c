// EXPECT_COMPILE_ERROR
// Test expression recovery with multiple errors
// Should report multiple errors without cascading

int main() {
    // Multiple undefined variables
    int a = foo + bar;

    // Struct member errors
    struct {
        int x;
    } s;
    int b = s.y + s.z;

    // More undefined variables
    int c = baz * qux;

    return 42;
}
