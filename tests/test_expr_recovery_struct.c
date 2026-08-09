// EXPECT_COMPILE_ERROR
// Test struct member error recovery
// Should report member errors without cascading

struct Point {
    int x;
    int y;
};

int main() {
    struct Point p;
    p.x = 10;
    p.y = 20;

    // These should error - no such members
    int a = p.z;
    int b = p.w;

    // This should still work
    int c = p.x + p.y;

    return 42;
}
