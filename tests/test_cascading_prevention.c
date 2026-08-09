// EXPECT_COMPILE_ERROR
// Test that error type propagation prevents cascading errors
// Should only report the root cause, not secondary errors

int main() {
    // undefined_var will have error type
    int x = undefined_var;

    // These operations use x, but shouldn't report additional errors
    // because x has error type and it propagates
    int y = x + 5;
    int z = y * 2;
    int w = z - 10;

    // Another independent error
    int a = another_undefined;

    return 42;
}
